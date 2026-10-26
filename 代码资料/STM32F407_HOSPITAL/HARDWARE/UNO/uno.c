#include "uno.h"

/* ============================================================
 * uno.c —— AFE4490(代号 UNO) 血氧模块串口帧解析
 *   通道  ：USART6 @57600，每收到 1 字节由接收中断调用 UNO_ParseByte()
 *   帧格式：0A FA 0A 00 02 + 10 字节 payload + 00 0B，共 17 字节
 *   输出  ：解析结果写入 g_meas；IR 采样另入环形缓冲供波形显示
 *   依赖  ：uno.h（measurement_t / g_meas / UNO_WAVE_RING）
 * ============================================================ */

measurement_t g_meas;
uint8_t      g_new_frame = 0;

/* uwTick 在 sys.c 中定义（= SysTick 1ms 节拍，SPO2 与 BP 共用同一计数）。
 * 此处仅读不写，用于在解帧成功时给 g_meas.result_time_ms 打时间戳（时效标注）。 */
extern volatile uint32_t uwTick;

/* ---- 接收诊断（供 USART1 观察链路） ---- */
volatile uint32_t g_uno_rx_bytes = 0;   /* 收到的原始字节总数 */
volatile uint32_t g_uno_frames   = 0;   /* 解析成功的完整帧数 */
uint8_t  g_uno_hr        = 0;           /* UNO 模块自算的 HR(pl[9])，bpm；供诊断对照，不被 F407 峰检覆盖 */

/* 最近 32 字节的环形缓冲：即使波特率不对（收到乱码）也能看出"到底有没有在发" */
#define UNO_RING_SZ   32u
static uint8_t  uno_ring[UNO_RING_SZ];
static uint8_t  uno_ring_w = 0;         /* 写指针，仅中断改 */

/* ---- PPG 波形采样环形缓冲（单生产者=中断，单消费者=主循环，无需关中断） ----
 * 写入：UNO_ParseByte() 每解出一帧成功帧、更新完 g_meas 后入环一个 IR 原始值。
 * 读出：主循环调 UNO_WaveTake()。两个指针各只有一个写者，16 位读写天然原子。
 * 容量 UNO_WAVE_RING 必须是 2 的幂，取模用位与完成。 */
#define UNO_WAVE_MASK  (UNO_WAVE_RING - 1u)
static volatile int32_t  uno_wave[UNO_WAVE_RING];   /* IR 原始 ADC，未做任何缩放 */
static volatile uint16_t uno_wave_w = 0;    /* 写指针：只有中断改 */
static volatile uint16_t uno_wave_r = 0;    /* 读指针：只有主循环改 */
volatile uint16_t g_uno_wave_drop = 0;      /* 环满丢弃点数（诊断用，正常为 0） */

/* ---- CAN 专用 PPG 波形环（镜像 uno_wave，独立消费者 = canopen_app_produce） ----
 * 与屏显示环完全相同的机制，仅消费者不同，使 CAN 波形上送与屏显示互不争抢同一环。 */
static volatile uno_wave_can_t uno_wave_can[UNO_WAVE_RING];
static volatile uint16_t uno_wave_can_w = 0;
static volatile uint16_t uno_wave_can_r = 0;
volatile uint16_t g_uno_wave_can_drop = 0;  /* CAN 环满丢弃点数（诊断用，正常为 0） */

/* 按写入先后顺序取走最多 max 个波形点，返回实际取出个数；主循环上下文调用，不可在中断里调用 */
uint16_t UNO_WaveTake(int32_t *out, uint16_t max)
{
    uint16_t n = 0;
    if (out == 0) return 0;
    while (n < max && uno_wave_r != uno_wave_w) {
        out[n++] = uno_wave[uno_wave_r];
        uno_wave_r = (uint16_t)((uno_wave_r + 1u) & UNO_WAVE_MASK);
    }
    return n;
}

/* 当前积压点数：0 表示没有新采样，可用于早退判断 */
uint16_t UNO_WavePending(void)
{
    return (uint16_t)((uno_wave_w - uno_wave_r) & UNO_WAVE_MASK);
}

/* 丢弃当前全部积压（暂停/恢复监护时调用，避免恢复瞬间把积压一次冲出） */
void UNO_WaveClear(void)
{
    uno_wave_r = uno_wave_w;
}

/* ---- CAN 专用波形环访问函数（消费者 = canopen_app_produce，主循环上下文调用） ---- */
uint16_t UNO_WaveTakeCAN(int32_t *out_wave, uint32_t *out_tick, uint16_t max)
{
    uint16_t n = 0;
    if (out_wave == 0 || out_tick == 0) return 0;
    while (n < max && uno_wave_can_r != uno_wave_can_w) {
        out_wave[n]    = uno_wave_can[uno_wave_can_r].ir_wave;
        out_tick[n]    = uno_wave_can[uno_wave_can_r].sample_tick;
        n++;
        uno_wave_can_r = (uint16_t)((uno_wave_can_r + 1u) & UNO_WAVE_MASK);
    }
    return n;
}

uint16_t UNO_WavePendingCAN(void)
{
    return (uint16_t)((uno_wave_can_w - uno_wave_can_r) & UNO_WAVE_MASK);
}

void UNO_WaveClearCAN(void)
{
    uno_wave_can_r = uno_wave_can_w;
}

/* ------------------------------------------------------------------
 * UNO 帧格式（基于 AFE4490_library 的 Example-1 二进制帧，兼容 afe_bridge.py）
 *   偏移  内容
 *   [0]   0x0A          同步1
 *   [1]   0xFA          同步2
 *   [2]   0x0A          数据长度低（DATA_LEN = 10）
 *   [3]   0x00          数据长度高
 *   [4]   0x02          类型 = DATA
 *   [5..8]  IR_data     int32 小端（LED1VAL）
 *   [9..12] RED_data    int32 小端（LED2VAL）
 *   [13]   spo2         uint8（0~100；未算出或探头错误时为 0）
 *   [14]   hr           uint8（bpm；0 表示无效）
 *   [15]   0x00         保留
 *   [16]   0x0B          帧尾
 * 共 17 字节。spo2/hr 仅在 AFE 算法算完后更新，其余时间保持上次值。
 * 同步策略：先匹配 5 字节头（0A FA 0A 00 02），再收 10 字节负载 + 帧尾 00 0B。
 * 即使 IR/RED 数据里出现 0x0A/0xFA 也不会误同步，因为头是固定 5 字节序列，
 * 且末尾再用 00 0B 二次校验。
 * ------------------------------------------------------------------ */

/* 解析状态机：按 [5 字节头] -> [10 字节 payload] -> [2 字节帧尾] 顺序推进 */
typedef enum {
    S0_SYNC1, S1_SYNC2, S2_LENL, S3_LENH, S4_TYPE,
    S5_PAYLOAD, S6_FOOT1, S7_FOOT2
} parse_state_t;

static parse_state_t st     = S0_SYNC1;   /* 当前解析状态 */
static uint8_t       pl[10];              /* payload 缓存，长度固定 10 字节 */
static uint8_t       pl_i   = 0;          /* payload 已收字节数 */
static uint8_t       foot1  = 0;          /* 帧尾首字节，等待第二字节二次校验 */

/* 读取小端 4 字节有符号整数（帧内 IR/RED 字段为 int32 小端） */
static int32_t rd_i32_le(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0]) |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

/* 每收到 USART6 一个字节调用一次（中断上下文，不可阻塞、不可重入）。
 * 先计数并入诊断环，再走状态机；仅当完整帧校验通过才更新 g_meas 并置 g_new_frame。 */
void UNO_ParseByte(uint8_t b)
{
    /* 先记账 + 入环，再做状态机判定：这样即使波特率错、同步不上，
       调试口也能看到"字节在进"以及进的到底是什么。 */
    g_uno_rx_bytes++;
    uno_ring[uno_ring_w] = b;
    uno_ring_w = (uint8_t)((uno_ring_w + 1u) & (UNO_RING_SZ - 1u));

    switch (st) {
    case S0_SYNC1:
        if (b == 0x0A) st = S1_SYNC2;
        break;
    case S1_SYNC2:
        st = (b == 0xFA) ? S2_LENL : S0_SYNC1;
        break;
    case S2_LENL:
        st = (b == 0x0A) ? S3_LENH : S0_SYNC1;
        break;
    case S3_LENH:
        st = (b == 0x00) ? S4_TYPE : S0_SYNC1;
        break;
    case S4_TYPE:
        pl_i = 0;
        st = (b == 0x02) ? S5_PAYLOAD : S0_SYNC1;
        break;
    case S5_PAYLOAD:
        pl[pl_i++] = b;
        if (pl_i >= 10) st = S6_FOOT1;
        break;
    case S6_FOOT1:
        foot1 = b;
        st = S7_FOOT2;
        break;
    case S7_FOOT2:
        if (foot1 == 0x00 && b == 0x0B) {
            g_uno_frames++;
            g_meas.ir_raw        = rd_i32_le(&pl[0]);       /* pl[0..3] = IR  */
            g_meas.red_raw       = rd_i32_le(&pl[4]);       /* pl[4..7] = RED */
            g_meas.spo2          = (int16_t)pl[8];
            /* 脉率由 F407 峰检计算，此处不写 g_meas.pulse_rate；UNO 原 HR 仅存到
             * g_uno_hr 供诊断对照，避免本中断与 on_uno_frame() 争写该变量。 */
            g_uno_hr             = pl[9];
            g_meas.result_valid  = (pl[8] != 0) ? 1 : 0;   /* spo2==0 视为无效 */
            g_meas.probe_state   = (pl[8] != 0) ? 1 : 0;
            /* 不再写 g_meas.signal_quality：该值由主循环 on_uno_frame() 写入真实 SQI，
               本函数在 USART6 中断里每帧执行，若在此写占位值会覆盖真实值。 */
            g_new_frame = 1;
            /* 时效标注：每次成功解帧都刷新时间戳——只要链路/算法仍在吐帧，
             * result_time_ms 就持续前移；一旦静默，age_ms 自然增长触发"陈旧"。 */
            g_meas.result_time_ms = uwTick;

            /* 波形入环：每个 IR 采样都进，不合并、不丢点（见 uno.h 的说明）。
             * 存原始值不右移，保留全部精度，缩放交给 dc_hmi.c 的自适应归一化。 */
            {
                uint16_t nw = (uint16_t)((uno_wave_w + 1u) & UNO_WAVE_MASK);
                if (nw != uno_wave_r) {
                    uno_wave[uno_wave_w] = g_meas.ir_raw;
                    uno_wave_w = nw;
                } else {
                    g_uno_wave_drop++;      /* 环满：主循环没跟上，丢弃并计数 */
                }
            }
            /* 同一采样并行入 CAN 专用环（独立消费者 canopen_app_produce，不抢屏环）。
             * 配对采样时刻 uwTick：ISR 在这里取，与 ir_wave 组成 (ir_wave, sample_tick) 对，
             * 供 2110:01/2110:02 分别上送，使 Gateway 能按 tick 做波形时间对齐。 */
            {
                uint16_t nwc = (uint16_t)((uno_wave_can_w + 1u) & UNO_WAVE_MASK);
                if (nwc != uno_wave_can_r) {
                    uno_wave_can[uno_wave_can_w].ir_wave     = g_meas.ir_raw;
                    uno_wave_can[uno_wave_can_w].sample_tick = uwTick;
                    uno_wave_can_w = nwc;
                } else {
                    g_uno_wave_can_drop++;
                }
            }
        }
        st = S0_SYNC1;
        break;
    default:
        st = S0_SYNC1;
        break;
    }
}

/* ============================================================
 * 诊断辅助
 * ============================================================ */
/* 复位解析状态机、诊断计数与波形环指针（切换波特率 / 重新同步前调用） */
void UNO_Reset(void)
{
    st      = S0_SYNC1;
    pl_i    = 0;
    foot1   = 0;
    g_uno_rx_bytes = 0;
    g_uno_frames   = 0;
    g_uno_wave_drop = 0;
    uno_wave_w = 0;
    uno_wave_r = 0;
    g_uno_wave_can_drop = 0;
    uno_wave_can_w = 0;
    uno_wave_can_r = 0;
}

/* 拷贝最近 n 个原始接收字节（按接收先后顺序）到 out，返回实际拷贝个数；n 上限 UNO_RING_SZ(32) */
uint8_t UNO_Snapshot(uint8_t *out, uint8_t n)
{
    uint8_t w, i;

    if (out == 0) return 0;
    if (n > (uint8_t)UNO_RING_SZ) n = (uint8_t)UNO_RING_SZ;

    w = uno_ring_w;   /* 单字节读，读一次即快照，避免半途被中断改掉 */
    for (i = 0; i < n; i++)
    {
        out[i] = uno_ring[(uint8_t)((w + UNO_RING_SZ - n + i) & (UNO_RING_SZ - 1u))];
    }
    return n;
}
