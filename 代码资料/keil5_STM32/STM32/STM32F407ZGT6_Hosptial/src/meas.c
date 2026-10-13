/*!
*  \file meas.c
*  \brief 测量数据源实现：内置演示信号发生器 / 真实 AFE4490 接入点
*
*  ---------------------------- 使用说明 ----------------------------
*  MEAS_USE_AFE4490 = 0（默认）
*      纯软件演示。上电就有接近真实的 PPG 波形、血氧、脉率、灌注质量，
*      用于在没有传感器的情况下验证 7 个画面的显示与交互。
*      想触发报警：进"设置"页把 SpO2 下限改到 99，几秒后就会报警。
*      想把"报警与探头状态"页的各种状态都看一遍：把 MEAS_DEMO_CYCLE 改成 1。
*
*  MEAS_USE_AFE4490 = 1
*      切换为真实 AFE4490。需要补全本文件底部 3 个标为 TODO 的静态函数，
*      并把 stm32f4xx_conf.h 里的 stm32f4xx_spi.h 打开、把 SPI 驱动的 .c
*      加进 Keil 工程。界面代码(app_spo2.c)完全不用改。
*  ------------------------------------------------------------------
*/
#include "meas.h"
#include <math.h>

/* ==================== 配置 ====================
*  MEAS_USE_AFE4490 / MEAS_DEMO_CYCLE 已移到 meas.h（main.c 也要用） */
#define DEMO_TICK_MS        10      /* 调用 Meas_Tick 的周期(ms)        */
#define VALUE_PERIOD_MS     500     /* 生理参数刷新周期(ms)             */

/* ==================== 全局测量结果 ==================== */
measurement_t g_meas;

static uint8 s_run = 1;             /* 1 监护运行中 / 0 暂停 */

/* ==================== 原始样本环形缓冲 ====================
*  采集侧（Meas_Tick）写入，日志/算法侧（Meas_SamplePop）弹出。
*  缓冲满时覆盖最老样本并计入 s_sbuf_drop —— 验收要求该值恒为 0。 */
#define SAMPLE_BUF_SIZE     256u        /* 必须是 2 的幂 */
static raw_sample_t s_sbuf[SAMPLE_BUF_SIZE];
static volatile uint16 s_sbuf_w = 0;    /* 写指针 */
static volatile uint16 s_sbuf_r = 0;    /* 读指针 */
static uint32 s_sbuf_drop = 0;          /* 被覆盖的样本数 */
static uint32 s_seq       = 0;          /* 样本序号，只增不减 */

extern volatile uint32 timer_tick_count;    /* ulitity.c 维护的 10ms 节拍 */

/* 每采到一个样本调用一次：编号 + 时间戳 + 写入环形缓冲 */
static void sample_push(int32 red, int32 ir)
{
    uint16 next = (uint16)((s_sbuf_w + 1u) & (SAMPLE_BUF_SIZE - 1u));

    s_seq++;
    g_meas.sample_seq   = s_seq;
    g_meas.timestamp_ms = timer_tick_count * 10u;

    s_sbuf[s_sbuf_w].seq  = s_seq;
    s_sbuf[s_sbuf_w].t_ms = g_meas.timestamp_ms;
    s_sbuf[s_sbuf_w].red  = red;
    s_sbuf[s_sbuf_w].ir   = ir;

    if (next == s_sbuf_r)               /* 满：覆盖最老样本 */
    {
        s_sbuf_r = (uint16)((s_sbuf_r + 1u) & (SAMPLE_BUF_SIZE - 1u));
        s_sbuf_drop++;
    }
    s_sbuf_w = next;
}

#if MEAS_USE_AFE4490

/* 真实器件接口，实现在本文件底部 */
static void  AFE4490_Init(void);
static int32 AFE4490_ReadSample(int32 *red, int32 *ir);
static uint8 AFE4490_ProbeState(void);

#else   /* ------------------ 演示信号发生器 ------------------ */

static uint32 s_tick_ms    = 0;     /* 累计毫秒       */
static int32  s_last_ppg   = 0;     /* 最近一个采样点 */
static uint8  s_demo_mode  = 1;     /* 0 无探头 / 1 正常 / 2 弱信号 */
static float  s_phase      = 0.0f;  /* 脉搏相位       */
static float  s_base_ph    = 0.0f;  /* 呼吸基线相位   */
static uint32 s_rnd        = 20260923u;

/* 简易伪随机，范围 -range .. +range */
static int32 rnd(int32 range)
{
    s_rnd = s_rnd * 1103515245u + 12345u;
    return (int32)((s_rnd >> 16) % (uint32)(2 * range + 1)) - range;
}

/* 生成一个 PPG 采样点。取样率 = 1000/DEMO_TICK_MS Hz
 * 波形 = 主波 + 重搏波 + 三次谐波，叠加缓慢呼吸基线漂移与噪声 */
static int32 demo_ppg(void)
{
    const float dt     = (float)DEMO_TICK_MS / 1000.0f;
    const float TWO_PI = 6.2831853f;
    float amp, base, v, pr;

    if (s_demo_mode == 0)
    {
        /* 探头未接入：接近满量程的平直线，与真实 AFE 未夹指时的现象一致 */
        return 2096921 + rnd(60);
    }

    amp = (s_demo_mode == 2) ? 2500.0f : 18000.0f;      /* 弱信号 / 正常 */
    pr  = (g_meas.pulse_rate > 0) ? (float)g_meas.pulse_rate : 72.0f;

    s_phase += TWO_PI * pr / 60.0f * dt;
    if (s_phase > TWO_PI) s_phase -= TWO_PI;

    s_base_ph += TWO_PI * 0.12f * dt;
    if (s_base_ph > TWO_PI) s_base_ph -= TWO_PI;

    v  =        sinf(s_phase);                          /* 主波   */
    v += 0.30f * sinf(2.0f * s_phase + 0.7f);           /* 重搏波 */
    v += 0.10f * sinf(3.0f * s_phase);

    base = 400000.0f * (1.0f + 0.06f * sinf(s_base_ph));

    return (int32)(base + amp * v) + rnd(220);
}

/* 刷新血氧 / 脉率 / 灌注质量 / 探头状态 */
static void meas_update_values(void)
{
    if (s_demo_mode == 0)
    {
        g_meas.probe      = PROBE_OFF;
        g_meas.quality    = 0;
        g_meas.valid      = 0;
        g_meas.spo2       = -1;
        g_meas.pulse_rate = -1;
        return;
    }

    g_meas.probe = (s_demo_mode == 2) ? PROBE_WEAK : PROBE_ON;

    /* SpO2 在 95~99 之间缓慢随机游走 */
    if (g_meas.spo2 < 0) g_meas.spo2 = 97;
    else
    {
        g_meas.spo2 += (int16)rnd(1);
        if (g_meas.spo2 < 95) g_meas.spo2 = 95;
        if (g_meas.spo2 > 99) g_meas.spo2 = 99;
    }

    /* 脉率在 68~80 之间缓慢随机游走 */
    if (g_meas.pulse_rate < 0) g_meas.pulse_rate = 72;
    else
    {
        g_meas.pulse_rate += (int16)rnd(1);
        if (g_meas.pulse_rate < 68) g_meas.pulse_rate = 68;
        if (g_meas.pulse_rate > 80) g_meas.pulse_rate = 80;
    }

    if (s_demo_mode == 2) g_meas.quality = (uint8)(32 + rnd(6));   /* 26~38  */
    else                  g_meas.quality = (uint8)(88 + rnd(4));   /* 84~92  */
    if (g_meas.quality > 100) g_meas.quality = 100;

    g_meas.valid = 1;
}

#endif  /* MEAS_USE_AFE4490 */

/* ==================== 对外接口 ==================== */
void Meas_Init(void)
{
    g_meas.sample_seq   = 0;
    g_meas.timestamp_ms = 0;
    g_meas.red_raw    = 0;
    g_meas.ir_raw     = 0;
    g_meas.spo2       = -1;
    g_meas.pulse_rate = -1;
    g_meas.quality    = 0;
    g_meas.probe      = PROBE_ON;
    g_meas.valid      = 0;
    g_meas.fault_code = 0;
    g_meas.algo_version = MEAS_ALGO_VERSION;
    s_run             = 1;

    s_sbuf_w    = 0;
    s_sbuf_r    = 0;
    s_sbuf_drop = 0;
    s_seq       = 0;

#if MEAS_USE_AFE4490
    AFE4490_Init();
    g_meas.probe = AFE4490_ProbeState();
#else
    s_tick_ms   = 0;
    s_last_ppg  = 0;
    s_phase     = 0.0f;
    s_base_ph   = 0.0f;
    s_demo_mode = 1;
#endif
}

void Meas_SetRun(uint8 run)
{
    s_run = run ? 1 : 0;
}

void Meas_Tick(void)
{
    if (!s_run) return;                     /* 暂停期间不出数、不推进波形 */

#if MEAS_USE_AFE4490
    {
        int32 red = 0, ir = 0;
        if (AFE4490_ReadSample(&red, &ir) == 0)
        {
            g_meas.red_raw = red;
            g_meas.ir_raw  = ir;
            sample_push(red, ir);
        }
        /* 血氧/脉率算法在此调用，结果写入 g_meas.spo2 / g_meas.pulse_rate */
    }
#else
    s_tick_ms += DEMO_TICK_MS;

#if MEAS_DEMO_CYCLE
    {
        uint8 seg = (uint8)((s_tick_ms / 40000u) % 3u);     /* 每 40s 换一种 */
        s_demo_mode = (seg == 0) ? 1 : ((seg == 1) ? 2 : 0);
    }
#endif

    if ((s_tick_ms % VALUE_PERIOD_MS) == 0)
    {
        meas_update_values();
    }

    s_last_ppg     = demo_ppg();
    g_meas.ir_raw  = s_last_ppg;
    g_meas.red_raw = s_last_ppg / 2 + 200000;
    sample_push(g_meas.red_raw, g_meas.ir_raw);
#endif
}

int32 Meas_PpgSample(void)
{
    if (!s_run) return 0;
    return s_last_ppg;
}

uint8 Meas_SamplePop(raw_sample_t *out)
{
    uint16 r = s_sbuf_r;

    if (r == s_sbuf_w) return 0;        /* 空 */

    *out = s_sbuf[r];
    s_sbuf_r = (uint16)((r + 1u) & (SAMPLE_BUF_SIZE - 1u));
    return 1;
}

uint32 Meas_SampleDrops(void)
{
    return s_sbuf_drop;
}

/* ==================================================================
 *  真实 AFE4490 接入点（仅当 MEAS_USE_AFE4490 = 1 时编译）
 * ------------------------------------------------------------------
 *  连线参考 F:\嵌入式\其他资料\AFE4490-SPI与DRDY控制信号详解.md：
 *      SPI : SCK / MISO / MOSI / CS
 *      DRDY: 数据就绪（可用中断，也可轮询）
 *      PWDN / RESET / START: 控制脚
 *  典型流程：复位 -> 写 0x00~0x31 初始化寄存器 -> START=1 ->
 *            等 DRDY -> 读 0x2A(IR) / 0x2B(RED) -> 做 SpO2 算法
 * ================================================================== */
#if MEAS_USE_AFE4490

static void AFE4490_Init(void)                                  /* TODO: 待实现 */
{
    /* 1) 开 SPI / GPIO 时钟，配置 SCK/MISO/MOSI/CS 与 DRDY/PWDN/RESET/START
       2) 复位：RESET 拉低 1ms 再拉高，延时 1ms
       3) 逐条写 AFE4490 官方寄存器配置表（0x00~0x31）
       4) 置 START=1，打开内部 ADC */
}

static int32 AFE4490_ReadSample(int32 *red, int32 *ir)          /* TODO: 待实现 */
{
    /* 等 DRDY 有效 -> 读寄存器 0x2A / 0x2B（24 位，最高位为符号位）
       成功返回 0，失败返回 -1 */
    *red = 0;
    *ir  = 0;
    return -1;
}

static uint8 AFE4490_ProbeState(void)                           /* TODO: 待实现 */
{
    /* 由 IR 交流分量幅度判断：极小 -> PROBE_OFF，偏小 -> PROBE_WEAK */
    return PROBE_ON;
}

#endif /* MEAS_USE_AFE4490 */
