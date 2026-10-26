#ifndef __UNO_H
#define __UNO_H

#include <stdint.h>

/* 血氧测量结果（应用层只读；由 uno.c 解析出 UNO 帧后写入） */
typedef struct {
    int32_t  red_raw;        /* RED 通道原始 ADC（来自 AFE4490 LED2VAL） */
    int32_t  ir_raw;         /* IR  通道原始 ADC（来自 AFE4490 LED1VAL） */
    int16_t  spo2;           /* 血氧 0~100；0 表示无效 / 探头错误 */
    int16_t  pulse_rate;     /* 脉率 PR，bpm；由 F407 峰检计算 */
    uint8_t  signal_quality; /* 信号质量 0~100；由主循环 on_uno_frame() 写入真实 SQI */
    uint8_t  probe_state;    /* 0 未接入 / 错误，1 接入 */
    uint8_t  result_valid;   /* 1 = 数值有效 */
    uint8_t  fault_code;
    uint32_t monitor_sec;    /* 监护时长（秒），由 main 累加 */
    /* ---- 时效标注（NodeA_未完成任务清单 §2.2：spo2/hr 时效标注）----
     * result_time_ms：最近一次"有效结果"落地的时间戳(ms, = uwTick/millis())。
     *   在 uno.c 每解出一帧成功帧(无论 spo2 是否有效)时刷新；在 app_bloodoxygen.c
     *   的 on_uno_frame 算出新 PR 时也刷新。链路/算法一旦静默便不再更新，age_ms 据此增长。
     * age_ms：当前展示值年龄(ms) = millis() - result_time_ms，由 task_hmi_value 刷新；
     *   超过 SPO2_STALE_MS 即判"数据陈旧"，屏转 -- 并不再把冻结的旧值伪装成新值。 */
    uint32_t result_time_ms;  /* 最近一次有效结果落地时间戳(ms) */
    uint32_t age_ms;         /* 当前展示值年龄(ms)，由 task_hmi_value 刷新 */
} measurement_t;

extern measurement_t g_meas;     /* 解析结果，应用层读取 */
/*! 收到一帧置 1。
 *  业务不再用它做波形门控 —— 单个布尔标志会把停顿期间到达的多帧合并成一点
 *  （波形"卡顿"/比例失真），波形改走下面的采样环。保留仅为兼容/诊断。 */
extern uint8_t      g_new_frame;

/* 接收诊断：用于在 USART1 上判断"UNO 到底有没有在发" */
extern volatile uint32_t g_uno_rx_bytes;  /* USART6 收到的原始字节总数 */
extern volatile uint32_t g_uno_frames;    /* 解析成功的完整帧数 */
extern uint8_t  g_uno_hr;                 /* UNO 自算 HR(pl[9])，bpm；供诊断对照，不被 F407 峰检覆盖 */

/* ---------------- PPG 波形采样环形缓冲 ----------------
 * 为什么需要：main() 是单线程事件循环，而血压屏走 9600 波特率、发送是"死等 TXE"
 * 的阻塞轮询（见 app_bp.c 的 uart_send_byte）。仅"时钟广播"一项每秒就要发 10 帧
 * 共约 340 字节 => 约 350ms 连续占用主循环。若波形只靠 g_new_frame 这一个布尔标志
 * 表示"有新帧"，这段停顿期间到达的多个 UNO 帧会被合并成 1 个波形点 —— 表现为
 * 波形"一秒卡一次"，且横向采样比例失真、脉冲间距忽宽忽窄。
 * 现在改成：中断里把**每一个** IR 采样入环，主循环每次把积压的点全部取走推给屏，
 * 主循环偶尔被别的任务占住也不会丢点。容量必须是 2 的幂。 */
#define UNO_WAVE_RING   256u

/* 环满导致被丢弃的点数（诊断用；正常应为 0） */
extern volatile uint16_t g_uno_wave_drop;

/* 取走最多 max 个波形点（IR 原始 ADC，未做任何缩放），返回实际取到个数。
 * 必须在主循环/任务上下文调用（不可在中断里调用）。 */
uint16_t UNO_WaveTake(int32_t *out, uint16_t max);

/* 当前积压点数：0 表示没有新采样，可用于早退判断 */
uint16_t UNO_WavePending(void);

/* 丢弃当前全部积压（暂停监护/恢复时调用，避免恢复瞬间把积压一次冲出） */
void UNO_WaveClear(void);

/* ---- PPG 波形采样环形缓冲（CAN 专用，独立于屏显示环） ----
 * 设计：USART6 中断里把每个 IR 采样【同时】推入屏显示环(uno_wave)与 CAN 环(uno_wave_can)，
 * 两个消费者各自独立取用：屏路径(on_uno_frame) 与 CANopen 路径(canopen_app_produce) 互不争抢，
 * 避免单消费者环被两次 UNO_WaveTake 互相掏空。容量同样为 2 的幂(UNO_WAVE_RING)。
 * CAN 环每个采样点与采样时刻 tick 配对：供 2110:01 ir_wave + 2110:02 sample_tick 上送，
 * 使 Gateway-C 能按 tick（与 2100:02 同域）做波形时间对齐。 */
typedef struct {
    int32_t  ir_wave;      /* IR 原始 ADC，未做缩放 */
    uint32_t sample_tick;  /* 采样时刻(uwTick, ms)，与 2100:02 同域 */
} uno_wave_can_t;

extern volatile uint16_t g_uno_wave_can_drop;     /* CAN 环满丢弃点数（诊断，正常为 0） */

/* 取走最多 max 个波形点：out_wave 收 IR 原始值、out_tick 收配对采样时刻(uwTick,ms)，返回实际取到个数。
 * 必须在主循环/任务上下文调用（不可在中断里调用）。out_wave 与 out_tick 须为长度 >= max 的有效缓冲。 */
uint16_t UNO_WaveTakeCAN(int32_t *out_wave, uint32_t *out_tick, uint16_t max);
uint16_t UNO_WavePendingCAN(void);
void     UNO_WaveClearCAN(void);

/* 每收到 UNO 一个字节调用一次（在 USART6 中断里） */
void UNO_ParseByte(uint8_t b);

/* 复位解析状态机与诊断计数（切换波特率前调用） */
void UNO_Reset(void);

/* 取最近 n 个原始字节（按接收先后顺序），返回实际拷贝个数。用于抓包回显。 */
uint8_t UNO_Snapshot(uint8_t *out, uint8_t n);

#endif /* __UNO_H */
