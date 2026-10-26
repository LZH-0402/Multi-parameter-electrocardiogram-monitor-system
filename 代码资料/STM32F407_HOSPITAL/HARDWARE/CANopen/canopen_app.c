/**
 * canopen_app.c - 监护仪 Node-A CANopen 应用封装层实现
 *
 * 详见 canopen_app.h。裸机 / SPL / Keil AC5，CO_USE_GLOBALS 静态对象模式。
 *
 * 数据流（§2.1 CANopen 上报 Gateway-C；标量 + 状态 + PPG 波形）：
 *   应用层(g_meas / bp_last_get / bp_state_get / UNO_WaveTakeCAN)
 *     -> canopen_app_produce() 写 OD RAM 区(2100~2130 标量/状态 + 2110 PPG 波形)
 *     -> 5 路 TPDO 周期上报（参考 5 路布局）：
 *          TPDO1 0x181 -> 2110 PPG 波形(eventTimer=8ms, transmissionType=0xFF, 125Hz 对齐 UNO 源)
 *          TPDO2 0x281 -> 2111 第二波形(Node-A 保留, 不发送, eventTimer=0 禁用)
 *          TPDO3 0x381 -> 2120 标量(SpO2/PR/NIBP_SYS/DIA, eventTimer=1000ms = 1Hz)
 *          TPDO4 0x481 -> 2100 时钟(epoch/tick, eventTimer=1000ms = 1Hz)
 *          TPDO5 0x681 -> 2130 有效性+启动代次(eventTimer=1000ms = 1Hz)
 *     -> Gateway-C 经 CAN 总线消费。
 *  波形源：UNO(AFE4490) 每帧 1 个 IR 原始 ADC 采样，在 USART6 中断里同时入
 *        "屏显示环(uno_wave)"与"CAN 专用环(uno_wave_can)"，两消费者互不争抢。
 */
#include "CANopen.h"
#include "canopen_app.h" /* 本模块头：CANOPEN_NODE_ID / CANOPEN_BITRATE 宏与对外声明 */
#include "OD.h"          /* OD_RAM / OD_entry 全局、OD_t* OD */
#include "app_bp.h"      /* bp_last_get / bp_state_get */
#include "uno.h"         /* g_meas：spo2 / pulse_rate / signal_quality / probe_state / result_valid */
#include "app_bloodoxygen.h" /* spo2_rtc_get_epoch：软 RTC 日历 -> Unix epoch 秒（2100:01 用） */
#include "sys.h"         /* uwTick（1ms 节拍） */

#include "stm32f4xx.h"
#include "stm32f4xx_can.h"   /* CAN1 外设宏 */

/* ============================================================================
 *  D8 定点单位常量（MPM 统一数据字典 v0.1 的"单位 / CAN 固定点类型"）
 *    SpO2 / PR  -> u16 / 100   （% / bpm，如 98.50% 上送 9850）
 *    NIBP SYS/DIA -> u16 / 10  （mmHg，如 120.5 上送 1205）
 *    PPG ir_wave -> relative, i32 / 1,000,000（2110:01）
 *      AFE4490 为 22-bit ADC，按 ADC 满量程 2^22 归一（用户确认）。
 * ========================================================================== */
#define SPO2_PR_SCALE    100     /* u16 / 100 */
#define NIBP_SCALE       10      /* u16 / 10  */
#define PPG_ADC_FULLSCALE 4194304 /* 2^22，AFE4490 22-bit ADC 满量程 */
#define PPG_SCALE_NUM    1000000 /* i32 / 1,000,000 的分子 */
/* D11：NIBP 有效位时效窗口。测量完成超过此时长、或新一轮测量进行中，CAN 的 NIBP 有效位即清（MPM：过期必须清位）。
 * 默认 30min；若自动测量间隔更长，请调大（或设为极大值即"有效直到被新一轮测量取代"）。 */
#define NIBP_STALE_MS   1800000u /* 30 * 60 * 1000 */

/* CO_CANinterrupt 在 CO_driver.c 中实现，但未在 301/CO_driver.h 声明原型，
 * 此处补 extern 声明（符合 CANopenNode 约定：中断入口由目标驱动提供）。 */
extern void CO_CANinterrupt(CO_CANmodule_t* CANmodule);

/* ============================================================================
 *  模块内部状态
 * ========================================================================== */
static CO_t* g_CO = NULL;            /* CO_USE_GLOBALS 下 CO_new 返回的全局对象指针 */
static uint32_t s_boot_generation = 0;   /* 上电/通信重置次数，写进 OD 2130:02 供 Gateway 判重启 */

/* data_valid_flags 位定义（Node-A 私有，Gateway-C 解析用），按 MPM 统一数据字典 v0.1
 * 的"有效位"列做分区规范（D9）：
 *   bit0 = PPG 有效（MPM A bit0；新增，原代码缺失）
 *   bit1 = SpO2 有效（MPM A bit1）
 *   bit2 = 脉率 PR 有效（MPM A bit2）
 *   bit3 = NIBP 有效（MPM A bit3；当前有效测量快照，时效由 D11 维护）
 *   bit4 = 探头已接入 PROBE_ON（Node-A 厂商扩展：MPM 有效位区之外）
 *   bit5 = NIBP 测量中（Node-A 厂商扩展）
 *   bit6 = NIBP 停止中（Node-A 厂商扩展）
 *   bit7 = 波形有采样可取 WAVE_VALID（Node-A 厂商扩展）
 *   bit31 = synthetic（真实 A/B 采集数据恒 0；合成/回放测试数据才置 1）
 * 说明：bit0-3 + bit31 严格对齐 MPM，Gateway 按规范解析即可；
 *       bit4-7 为 Node-A 私有扩展，与 MPM 定义区(0-3,31)不冲突；后续新增 Node-A 状态只占用 8-30。 */
#define DV_PPG_VALID     (1u << 0)   /* PPG 波形信号有效（MPM A bit0） */
#define DV_SPO2_VALID    (1u << 1)   /* SpO2 有效（MPM A bit1） */
#define DV_PR_VALID      (1u << 2)   /* 脉率 PR 有效（MPM A bit2） */
#define DV_NIBP_VALID    (1u << 3)   /* NIBP 有效测量快照（MPM A bit3） */
#define DV_PROBE_ON      (1u << 4)   /* 探头已接入（Node-A 扩展） */
#define DV_NIBP_MEAS     (1u << 5)   /* NIBP 测量中（Node-A 扩展） */
#define DV_NIBP_STOP     (1u << 6)   /* NIBP 停止中（Node-A 扩展） */
#define DV_WAVE_VALID    (1u << 7)   /* PPG 波形有采样可取（Node-A 扩展） */

/* ============================================================================
 *  协议栈初始化（CO_new 之后的全部 CANopen 初始化 + 进入正常模式）
 *  通信重置时也会再次调用本函数。
 * ========================================================================== */
static uint8_t canopen_app_stack_init(void)
{
    uint32_t errInfo = 0;
    CO_ReturnError_t err;

    if (g_CO == NULL) {
        return 1u;
    }

    /* 1) CAN 驱动：bxCAN(GPIO/NVIC/BTR/过滤器) + 进入配置模式 -> 正常模式 */
    err = CO_CANinit(g_CO, CAN1, CANOPEN_BITRATE);
    if (err != CO_ERROR_NO) {
        return (uint8_t)err;
    }

    /* 2) 通信对象初始化：NMT 上电即自主进入 OPERATIONAL（CO_NMT_STARTUP_TO_OPERATIONAL），
     *    无需 NMT 主机；OD_statusBits 传 1001h(错误寄存器)。 */
    err = CO_CANopenInit(g_CO, NULL, NULL, OD, OD_ENTRY_H1001,
                         CO_NMT_STARTUP_TO_OPERATIONAL,
                         500,   /* firstHBTime_ms：首发心跳延时 */
                         500,   /* SDO 服务器超时 */
                         500,   /* SDO 客户端超时（本工程未用客户端，仅占位） */
                         false, /* SDO 客户端块传输（未用） */
                         CANOPEN_NODE_ID,
                         &errInfo);
    if (err != CO_ERROR_NO) {
        return (uint8_t)err;
    }

    /* 3) PDO 对象初始化：必须在通信对象之后，否则 OD 变量未正确映射到 TPDO */
    err = CO_CANopenInitPDO(g_CO, g_CO->em, OD, CANOPEN_NODE_ID, &errInfo);
    if (err != CO_ERROR_NO) {
        return (uint8_t)err;
    }

    /* 4) 进入 CAN 正常模式（硬件已在 CO_CANmodule_init 内由 CAN_Init 进入正常模式，
     *    此处仅置 CANnormal 标志，允许 TPDO 发送） */
    CO_CANsetNormalMode(g_CO->CANmodule);

    /* 每次上电 / 通信重置，boot_generation 自增，供 Gateway 判断设备是否重启 */
    s_boot_generation++;
    OD_RAM.x2130_validity_generation.boot_generation = s_boot_generation;

    return 0u;
}

uint8_t canopen_app_init(void)
{
    /* CO_USE_GLOBALS：config 传 NULL，对象为全局静态，免 heap */
    g_CO = CO_new(NULL, NULL);
    if (g_CO == NULL) {
        return 1u;
    }
    return canopen_app_stack_init();
}

/* ============================================================================
 *  生产者：刷新 OD RAM 区(2100~2130)
 * ========================================================================== */
void canopen_app_produce(void)
{
    if (g_CO == NULL) {
        return;
    }

    bp_last_t last = bp_last_get();     /* 最近一次血压结果（只读副本） */
    bp_state_t st  = bp_state_get();    /* 血压状态机 */

    /* 2100 epoch/tick：
     *   2100:01 = 软 RTC 换算的 UTC epoch 秒（对象字典 v0.1 与 MPM 统一数据字典 v0.1 要求 2100:01 为"UTC epoch 秒"）；
     *   2100:02 = 节点单调毫秒节拍(uwTick)，与 2110:02 同域，供 Gateway 做时间对齐。
     * 注：原实现写 uwTick/1000（上电运行秒），与规范"UTC epoch 秒"不符，D7 已修正。 */
    OD_RAM.x2100_epoch_tick.epoch = spo2_rtc_get_epoch();
    OD_RAM.x2100_epoch_tick.tick  = (uint32_t)uwTick;

    /* 2120 生命体征：按 MPM 统一数据字典 v0.1 定点单位上送（D8 修正）
     *   SpO2 -> u16/100（%）   PR -> u16/100（bpm）   NIBP -> u16/10（mmHg）
     * 源值均为整数（spo2/pulse_rate 为 int16，sys/dia 为 uint16），×比例后仍在 u16 范围内。 */
    OD_RAM.x2120_vitals.SpO2     = (uint16_t)((int32_t)g_meas.spo2 * SPO2_PR_SCALE);
    OD_RAM.x2120_vitals.PR       = (uint16_t)((int32_t)g_meas.pulse_rate * SPO2_PR_SCALE);
    OD_RAM.x2120_vitals.NIBP_SYS = (uint16_t)(last.valid ? ((uint32_t)last.sys * NIBP_SCALE) : 0u);
    OD_RAM.x2120_vitals.NIBP_DIA = (uint16_t)(last.valid ? ((uint32_t)last.dia * NIBP_SCALE) : 0u);

    /* 2140(SQI / 探头状态) 已在 D3 从对象字典删除：参考规范对象字典仅定义
     * 2100/2110/2111/2120/2130，无 2140。其中 probe_state 已由 2130:01 的
     * DV_PROBE_ON 位承载（见下方 2130 组装）；连续 SQI 在参考规范中无对应字段，
     * 不再单独上送（屏端仍用 g_meas.signal_quality 显示）。
     * 2150(NIBP 脉率 / 血压状态机) 已在 D2 删除：NIBP 状态由 2130:01 的
     * DV_NIBP_* 位承载，NIBP 脉率与 SpO2 脉率同源，暂不上送。 */

    /* 2110 PPG 波形（TPDO1, COB-ID 0x181）：从 CAN 专用波形环取一个 (IR采样, 采样时刻tick) 配对写入并请求发送。
     * ir_wave 写 2110:01、sample_tick 写 2110:02（与 2100:02 同域），供 Gateway 按 tick 做波形时间对齐。
     * 实际发送节奏由 0x1800.eventTimer(默认 8ms) 限制；若 UNO 的 PPG 采样率更高，调小该 eventTimer。 */
    {
        int32_t  wbuf[1];
        uint32_t tbuf[1];
        if (UNO_WaveTakeCAN(wbuf, tbuf, 1) == 1u) {
            /* PPG 波形：原始 ADC 计数按 ADC 满量程 2^22 归一为 relative(i32/1,000,000)，
             * 对齐 MPM 统一数据字典 v0.1 的 2110:01 固定点类型（D8）。
             * 用 int64 中间量避免 1000000×2^21 溢出；CAN 专用环仍存裸计数，屏端自适应归一化不受影响。 */
            OD_RAM.x2110_ppg_waveform.ir_wave     = (int32_t)(((int64_t)wbuf[0] * PPG_SCALE_NUM) / PPG_ADC_FULLSCALE);
            OD_RAM.x2110_ppg_waveform.sample_tick = tbuf[0];
            if (g_CO->TPDO != NULL) {
                CO_TPDOsendRequest(&g_CO->TPDO[0]);
            }
        }
    }

    /* 2130 有效性标志(u32 掩码) + 启动代次（boot_generation 在 init/通信重置时已设）。
     * 2130:01 位图已按 MPM 统一数据字典 v0.1 的"有效位"列做分区规范（D9）：
     *   bit0-3 严格对齐 MPM（PPG/SpO2/PR/NIBP 有效），bit31 = synthetic（真实 A/B 数据恒 0）；
     *   bit4-7 为 Node-A 厂商扩展（探头/NIBP 状态/波形可取），不与 MPM 区冲突。
     *   注：bit3(NIBP 有效) 的时效语义（过期/失败/未测量必须清位）由 D11 维护，此处先映射正确位。 */
    uint32_t flags = 0u;
    if (g_meas.probe_state != 0u)                        flags |= DV_PPG_VALID;  /* bit0 PPG 有效（MPM A bit0） */
    if ((g_meas.spo2 > 0) && (g_meas.result_valid != 0u)) flags |= DV_SPO2_VALID; /* bit1 SpO2 有效 */
    if (g_meas.pulse_rate > 0)                            flags |= DV_PR_VALID;   /* bit2 PR 有效 */
    /* NIBP 有效位（bit3，MPM A bit3）：按真实测量时效维护（D11）。
     *   条件：结果为有效 且 当前不在新一轮测量/停止中（旧结果被取代）且 未超过时效窗口 NIBP_STALE_MS。 */
    uint32_t nibp_age = (uwTick >= last.valid_tick)
                        ? (uwTick - last.valid_tick)
                        : (0xFFFFFFFFu - last.valid_tick + uwTick + 1u);
    uint8_t nibp_valid = ((last.valid != 0u) && (st != ST_MEASURING) && (st != ST_STOPPING)
                          && (nibp_age <= NIBP_STALE_MS)) ? 1u : 0u;
    if (nibp_valid) flags |= DV_NIBP_VALID; /* bit3 NIBP 有效快照 */
    if (g_meas.probe_state != 0u)                         flags |= DV_PROBE_ON;  /* bit4 探头接入（扩展） */
    if (st == ST_MEASURING)                               flags |= DV_NIBP_MEAS;  /* bit5 NIBP 测量中（扩展） */
    if (st == ST_STOPPING)                                flags |= DV_NIBP_STOP;  /* bit6 NIBP 停止中（扩展） */
    if (UNO_WavePendingCAN() > 0u)                        flags |= DV_WAVE_VALID;/* bit7 波形有采样可取（扩展） */
    OD_RAM.x2130_validity_generation.data_valid_flags = flags;
}

/* ============================================================================
 *  主循环周期处理
 * ========================================================================== */
void canopen_app_process(void)
{
    uint32_t timer_next;
    uint32_t diff_us;
    static uint32_t s_last_tick = 0u;
    static uint8_t  s_first = 1u;

    if (g_CO == NULL) {
        return;
    }

    /* 以 uwTick(1ms) 自算与上次调用的时间差(us)，并处理 32 位回绕 */
    {
        uint32_t now = uwTick;
        if (s_first != 0u) {
            s_first = 0u;
            s_last_tick = now;
            diff_us = 0u;
        } else if (now >= s_last_tick) {
            diff_us = (now - s_last_tick) * 1000U;
        } else {
            diff_us = ((0xFFFFFFFFU - s_last_tick) + now + 1U) * 1000U;
        }
        s_last_tick = now;
    }

    /* CO_process：异步对象（NMT/EM/SDO/HB/TIME/LED）处理，返回重置请求 */
    timer_next = 1000000u;   /* 初始上限，各 process 会据此缩短下一唤醒 */
    CO_NMT_reset_cmd_t reset = CO_process(g_CO, false, diff_us, &timer_next);

    if ((reset == CO_RESET_COMM) || (reset == CO_RESET_NOT)) {
        /* 通信 / 应用重置：重跑初始化（CO_USE_GLOBALS 下对象本身不需重建） */
        (void)canopen_app_stack_init();
        return;
    }

    /* 先刷新数据源，再处理 TPDO（确保最新值被映射进本周期报文） */
    canopen_app_produce();

    bool_t syncWas = CO_process_SYNC(g_CO, diff_us, &timer_next);
    CO_process_RPDO(g_CO, syncWas, diff_us, &timer_next);
    CO_process_TPDO(g_CO, syncWas, diff_us, &timer_next);
}

/* ============================================================================
 *  CAN 中断统一入口封装（供 stm32f4xx_it.c 的三路 CAN 向量调用）
 * ========================================================================== */
void canopen_app_CAN_interrupt(void)
{
    if (g_CO != NULL) {
        CO_CANinterrupt(g_CO->CANmodule);
    }
}

/* ============================================================================
 *  故障 EMCY 便捷封装
 *  注意 CO_errorReport 宏实参顺序为 (em, errorBit, errorCode, infoCode)：
 *    errorBit  = 错误寄存器位（errReg）；errorCode = 紧急错误码（errCode）；
 *    infoCode  = 附加信息（subErr）。
 *  设备专用（厂商）错误码取 0x1000 起。
 * ========================================================================== */
void canopen_app_emcy(uint16_t errCode, uint8_t errReg, uint8_t subErr)
{
    if ((g_CO != NULL) && (g_CO->em != NULL)) {
        CO_errorReport(g_CO->em, errReg, errCode, subErr);
    }
}
