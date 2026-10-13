/*!
 * \file dc_hmi.c
 * \brief 大彩串口屏下行驱动实现（SPL / USART2 移植版）
 *
 * 所有指令字节均取自 M 系列开发包 hmi_driver.c（路径见各函数注释），
 * 并经本机 VisualTFT 的 InstructConfig.xml 交叉核对 EE/B1/10 部分。
 *
 * 帧:   EE <cmd> [sub] <screen:16> <ctrl:16> <param...> FF FC FF FF
 * 注意: 16/32 位参数高字节在前；字符串不带 '\0'
 *
 * 移植说明（相对原 HAL 版）：
 *   - 删除 #include "stm32f4xx_hal.h" 与 extern UART_HandleTypeDef huart3
 *   - 底部 hmi_tx() 的实际实现改到 usart.c（用 USART2 发送），此处的弱定义仅占位
 *   - 新增 HMI_Init()（屏无需握手，这里留作扩展）
 *   其余拼帧 / 曲线 / 批量 / 通知解析逻辑一字未改。
 */

#include "dc_hmi.h"
#include "hmi_ids.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef __weak
#define __weak __attribute__((weak))
#endif

/* ============================================================
 * 1. 底层串口（SPL 版：实际发送在 usart.c 的 hmi_tx() 里用 USART2 完成）
 * ============================================================ */
__weak void hmi_tx(const uint8_t *p, uint16_t n)
{
    /* 占位：真正的实现由 usart.c 提供（USART2 轮询发送）。
       若某工程不接屏、也不链接 usart.c，则发送为空，不会报错。 */
    (void)p;
    (void)n;
}

/* ============================================================
 * 2. 帧组装原语
 * ============================================================ */
static uint8_t  s_tx[HMI_TX_BUF_SIZE];
static uint16_t s_len;

static void put8(uint8_t v)
{
    if (s_len < HMI_TX_BUF_SIZE) s_tx[s_len++] = v;
}
static void put16(uint16_t v)
{
    put8((uint8_t)(v >> 8));
    put8((uint8_t)(v & 0xFF));
}
static void put32(uint32_t v)
{
    put8((uint8_t)(v >> 24));
    put8((uint8_t)(v >> 16));
    put8((uint8_t)(v >> 8));
    put8((uint8_t)(v & 0xFF));
}
static void putstr(const char *s)
{
    while (s && *s) put8((uint8_t)*s++);
}

static void begin_cmd(void)   /* 帧头 0xEE */
{
    s_len = 0;
    put8(0xEE);
}
static void end_cmd(void)     /* 帧尾 FF FC FF FF */
{
    put8(0xFF); put8(0xFC); put8(0xFF); put8(0xFF);
    hmi_tx(s_tx, s_len);
}

/* ============================================================
 * 3. 控件写入
 *    通用写法是 EE B1 10 <screen> <ctrl> <数据>，文本/按钮/进度条/滑块都走它
 * ============================================================ */

/* 源: hmi_driver.c:835  EE B1 00 <screen> */
void HMI_SetScreen(uint16_t screen_id)
{
    begin_cmd();
    put8(0xB1); put8(0x00);
    put16(screen_id);
    end_cmd();
}

/* 源: hmi_driver.c  EE B1 01（查询当前画面，屏会应答 EE B1 01 <画面:16> FF FC FF FF）
 * 这是唯一能一次性判定“接线 + 波特率 + 帧格式”三项都正确的指令：
 * 只要屏有应答，链路就是通的。 */
void HMI_GetScreen(void)
{
    begin_cmd();
    put8(0xB1); put8(0x01);
    end_cmd();
}

/* 源: hmi_driver.c:929  EE B1 10 <screen> <ctrl> <字符串> */
void HMI_SetText(uint16_t screen_id, uint16_t ctrl_id, const char *str)
{
    begin_cmd();
    put8(0xB1); put8(0x10);
    put16(screen_id); put16(ctrl_id);
    putstr(str);
    end_cmd();
}

/* 源: hmi_driver.c:949  EE B1 07 <screen> <ctrl> <sign> <0x80|digits> <u32>
 * 注意：要求屏固件 >= 908。若写完没反应，请改用 HMI_SetText() 自己 sprintf */
void HMI_SetTextInt(uint16_t screen_id, uint16_t ctrl_id, int32_t value, uint8_t digits)
{
    uint8_t sign = (value < 0) ? 1 : 0;
    uint32_t mag = sign ? (uint32_t)(-value) : (uint32_t)value;

    begin_cmd();
    put8(0xB1); put8(0x07);
    put16(screen_id); put16(ctrl_id);
    put8(sign);
    put8((uint8_t)((digits & 0x0F) | 0x80));
    put32(mag);
    end_cmd();
}

/* 源: hmi_driver.c:999  EE B1 10 <screen> <ctrl> <u32> */
void HMI_SetProgress(uint16_t screen_id, uint16_t ctrl_id, uint32_t value)
{
    begin_cmd();
    put8(0xB1); put8(0x10);
    put16(screen_id); put16(ctrl_id);
    put32(value);
    end_cmd();
}

/* 源: hmi_driver.c:913  EE B1 10 <screen> <ctrl> <u8> */
void HMI_SetButton(uint16_t screen_id, uint16_t ctrl_id, uint8_t state)
{
    begin_cmd();
    put8(0xB1); put8(0x10);
    put16(screen_id); put16(ctrl_id);
    put8(state);
    end_cmd();
}

/* 源: hmi_driver.c:881  EE B1 03 <screen> <ctrl> <visible>
 * visible=1 显示 / 0 隐藏。用于"暂停监护 ↔ 继续监护"两个叠放按钮的切换。 */
void HMI_SetControlVisible(uint16_t screen_id, uint16_t ctrl_id, uint8_t visible)
{
    begin_cmd();
    put8(0xB1); put8(0x03);
    put16(screen_id); put16(ctrl_id);
    put8(visible);
    end_cmd();
}

/* 源: hmi_driver.c:897  EE B1 04 <screen> <ctrl> <enable> */
void HMI_SetControlEnable(uint16_t screen_id, uint16_t ctrl_id, uint8_t enable)
{
    begin_cmd();
    put8(0xB1); put8(0x04);
    put16(screen_id); put16(ctrl_id);
    put8(enable);
    end_cmd();
}

/* ============================================================
 * 4. 实时曲线 graph（PPG 波形）
 * ============================================================ */

/* 源: hmi_driver.c:1236  EE B1 33 <screen> <ctrl> <channel> */
void HMI_GraphClear(uint16_t screen_id, uint16_t ctrl_id, uint8_t channel)
{
    begin_cmd();
    put8(0xB1); put8(0x33);
    put16(screen_id); put16(ctrl_id);
    put8(channel);
    end_cmd();
}

/* 源: hmi_driver.c:1218  EE B1 32 <screen> <ctrl> <channel> <len:16> <data...>
 * 每个数据点 1 字节，取值必须落在控件配置的 start_value~end_value（本工程 0~255） */
void HMI_GraphAddData(uint16_t screen_id, uint16_t ctrl_id, uint8_t channel,
                      const uint8_t *data, uint16_t len)
{
    uint16_t i;
    if (len == 0) return;
    if (len > HMI_TX_BUF_SIZE - 20) len = HMI_TX_BUF_SIZE - 20;

    begin_cmd();
    put8(0xB1); put8(0x32);
    put16(screen_id); put16(ctrl_id);
    put8(channel);
    put16(len);
    for (i = 0; i < len; i++) put8(data[i]);
    end_cmd();
}

/* ============================================================
 * 5. 历史曲线 historygraph（趋势）
 * ============================================================ */

/* 源: hmi_driver.c:1687  EE B1 61 <screen> <ctrl> <enable> */
void HMI_HistoryEnable(uint16_t screen_id, uint16_t ctrl_id, uint8_t enable)
{
    begin_cmd();
    put8(0xB1); put8(0x61);
    put16(screen_id); put16(ctrl_id);
    put8(enable);
    end_cmd();
}

/* 源: hmi_driver.c:1721  EE B1 63 <screen> <ctrl> 00 <count:16>
 * count = 一屏显示的采样点数。本工程 sample_cycle=60s，
 *   30 分钟 -> 30 点；2 小时 -> 120 点；8 小时 -> 480 点 */
void HMI_HistoryTimeLength(uint16_t screen_id, uint16_t ctrl_id, uint16_t count)
{
    begin_cmd();
    put8(0xB1); put8(0x63);
    put16(screen_id); put16(ctrl_id);
    put8(0x00);
    put16(count);
    end_cmd();
}

/* 源: hmi_driver.c:1621  EE B1 60 <screen> <ctrl> <各通道数据:16*n>
 * 每调用一次 = 追加一个时间点的采样（多通道时按顺序给各通道值） */
void HMI_HistorySampleU16(uint16_t screen_id, uint16_t ctrl_id,
                          const uint16_t *value, uint8_t channel_cnt)
{
    uint8_t i;
    begin_cmd();
    put8(0xB1); put8(0x60);
    put16(screen_id); put16(ctrl_id);
    for (i = 0; i < channel_cnt; i++) put16(value[i]);
    end_cmd();
}

/* ============================================================
 * 6. 批量更新
 *    源: hmi_driver.c:1272 BatchBegin / 1342 BatchSetText / 1295 BatchSetProgress / 1389 BatchEnd
 *    一条帧里塞多个控件，帧数少、带宽省，推荐用于每拍的数值刷新
 * ============================================================ */
void HMI_BatchBegin(uint16_t screen_id)
{
    begin_cmd();
    put8(0xB1); put8(0x12);
    put16(screen_id);
}

/* <ctrl:16><len:16><字符串> */
void HMI_BatchSetText(uint16_t ctrl_id, const char *str)
{
    uint32_t n = str ? (uint32_t)strlen(str) : 0;
    put16(ctrl_id);
    put16((uint16_t)n);
    if (n) {
        const char *p = str;
        while (*p) put8((uint8_t)*p++);
    }
}

/* <ctrl:16><4:16><u32> */
void HMI_BatchSetProgress(uint16_t ctrl_id, uint32_t value)
{
    put16(ctrl_id);
    put16(4);
    put32(value);
}

void HMI_BatchEnd(void)
{
    end_cmd();
}

/* 源: hmi_driver.c:852  EE B3 <enable> */
void HMI_ScreenUpdateEnable(uint8_t enable)
{
    begin_cmd();
    put8(0xB3);
    put8(enable);
    end_cmd();
}

/* ============================================================
 * 6.5 系统：RTC 时间 / 背光 / 蜂鸣器
 *     指令集 V5.1：2.38 RTC时钟设置(81) / 2.39 读取RTC(82) / 背光(60) / 蜂鸣器(61)
 * ============================================================ */

static uint8_t bin2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

static uint8_t bcd2bin(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10u) + (v & 0x0F));
}

/* EE 81 <Sec> <Min> <Hour> <Day> <Week> <Mon> <Year> FF FC FF FF  （全部 BCD） */
void HMI_SetRTCTime(uint8_t year2, uint8_t mon, uint8_t day,
                    uint8_t hour, uint8_t min, uint8_t sec, uint8_t week)
{
    begin_cmd();
    put8(0x81);
    put8(bin2bcd(sec));
    put8(bin2bcd(min));
    put8(bin2bcd(hour));
    put8(bin2bcd(day));
    put8(bin2bcd(week));
    put8(bin2bcd(mon));
    put8(bin2bcd(year2));
    end_cmd();
}

/* EE 82 FF FC FF FF */
void HMI_ReadRTC(void)
{
    begin_cmd();
    put8(0x82);
    end_cmd();
}

/* EE 60 <level>，level 0=最亮 .. 255=最暗 */
void HMI_SetBackLight(uint8_t level)
{
    begin_cmd();
    put8(0x60);
    put8(level);
    end_cmd();
}

/* EE 61 <n>，n 单位 10ms */
void HMI_Beep(uint16_t ms)
{
    uint32_t n = ms / 10u;
    if (n > 255u) n = 255u;
    begin_cmd();
    put8(0x61);
    put8((uint8_t)n);
    end_cmd();
}

/* ============================================================
 * 7. 波形：原始 PPG -> 0~255 -> 批量下发
 * ============================================================
 * 目标观感（标准 pleth）：静息基线贴近屏幕底部，每个脉搏向上拱起；无论灌注强弱，
 * 波形纵向都撑满控件，一眼能看出节律、幅度和形态（上升支/重搏切迹）。
 *
 * 做法 = 自适应归一化（取代原来的"固定增益"）：
 *   1) 慢 EMA 跟踪基线 DC，只去掉大直流和缓慢漂移；
 *   2) 对交流分量 AC 做"快起慢落"的上下包络跟踪：
 *        上升沿立即跟随（不漏掉收缩期峰值），回落按 WAVE_ENV_SHIFT 缓慢衰减
 *        （量程不会因某个极端值滑出而突然塌陷/跳动）；
 *   3) 把 [lo, hi] 线性映射到 [WAVE_BASE, WAVE_TOP] —— 每个脉搏都撑满量程；
 *   4) 增益封顶：当包络跨度相对 DC 太小时（弱灌注/无脉搏/只剩噪声），
 *        不把跨度当成真实幅度去无限放大，避免"一条乱抖的线"。
 *
 * ★ 为什么必须自适应（原固定增益为什么"不直观"）：
 *   AFE4490 的 IR 交流幅度同时受 LED 电流、TIA 增益、指夹贴合度、末梢灌注影响，
 *   实测可差数十倍。固定增益 ×1.5625（旧 WAVE_FIXED_GAIN=400）下必然二选一：
 *     弱灌注 -> 交流只有几个计数，乘完几乎不动，看起来"一条平线/没波形"；
 *     强灌注 -> 交流上千计数，乘完越过 0/255 被硬削幅，看起来"方波、没有脉搏形状"。
 *   自适应归一化后，强弱只体现在包络的疏密上，"看不看得见"不再取决于灌注。
 *
 * ★ 采样率说明：入环/推入的是**未右移**的原始 ADC 值。旧实现曾做 ir_raw>>8，
 *   弱信号时交流只剩个位数计数，量化台阶明显（波形呈锯齿楼梯状）；现在保留全精度。
 *
 * ---- 可调旋钮（改了要重新烧录观察） ----
 *   WAVE_BASE / WAVE_TOP : 基线与峰值在 0~255 里的位置。
 *       若屏上波形"从顶部向下跳"（说明控件 0/255 方向与假设相反），
 *       把 WAVE_BASE 改成 232、WAVE_TOP 改成 8 即可整条翻转；
 *       想抬高/压低基线只调 WAVE_BASE。
 *   WAVE_DC_SHIFT : 越小基线跟得越快 = 高通截止越高 = 呼吸漂移抑制越好，
 *       但太小会把脉搏本身也削掉（相位失真）。125Hz 下 5≈0.6Hz、6≈0.3Hz。
 *   WAVE_ENV_SHIFT : 越大包络回落越慢 = 量程越稳、越不易"呼吸"，
 *       但遇到大幅度扰动后恢复满幅的时间也越长。8 约合 2 秒。
 *   WAVE_MIN_SPAN_DIV : 增益封顶阈值（跨度的相对下限 = DC/该值）。
 *       波形抖成一团噪声 -> 调大；弱灌注时波形偏小不够显眼 -> 调小。 */
#define WAVE_DC_SHIFT      5      /* 基线 EMA：dc += (raw-dc)>>5 */
#define WAVE_ENV_SHIFT     8      /* 包络回落：约 256 点（2s @125Hz）衰减到 63% */
#define WAVE_BASE          24     /* 静息基线位置(0~255)：小 = 贴底、脉搏向上跳 */
#define WAVE_TOP           248    /* 峰值位置(0~255)：顶部留白，防顶格削平 */
#define WAVE_MIN_SPAN_DIV  400    /* 跨度下限 = DC/400（约 0.25%）：增益封顶用的 */
#define WAVE_MIN_SPAN_ABS  4      /* 跨度绝对下限（DC 很小/接近 0 时的兜底） */
#define AFE_RAW_LIMIT      (1 << 21)  /* 输入限幅：AFE4490 为 22bit 二补码，±2^21-1 */

#define WAVE_RANGE         (WAVE_TOP - WAVE_BASE)

static struct {
    uint8_t  pts[HMI_WAVE_CHUNK];
    uint8_t  cnt;
    int32_t  dc;       /* 基线：慢 EMA 跟踪，去直流与缓慢漂移 */
    int32_t  hi;       /* AC 上包络（收缩期峰值侧） */
    int32_t  lo;       /* AC 下包络（舒张期谷值侧） */
    uint8_t  primed;   /* 0 = 还没拿到第一个采样，基线尚未建立 */
} s_wave;

/* ============================================================
 * 7.5 PPG 真实处理：数字滤波 + 峰值检测(PR) + 信号质量(SQI)
 *    ★ 2026-10-13 新增（依据《PPG波形分析与改进建议》#2/#3/#4）
 *
 * 背景：UNO 端报来的 PR 由 RED 通道经 hr_algorithm 算出，但该算法无幅度阈值、
 *   movingWindowSize=freq/50=2 几乎不平滑，高频毛刺常被当峰计成 ~208bpm 的"假心跳"。
 *   这里在 F407 端用**屏上显示的同一路 IR 波形**重算 PR + 真实 SQI，与显示同源，
 *   且不改动 UNO 固件、不改帧格式。
 *
 * 采样率 Fs：用户实测 ≈121Hz（示例1 loop 内 delay(8)+采样），故滤波/峰距按 121 计。
 *
 * 数字滤波链（每入一个 IR 原始采样）：
 *   1) 慢 EMA 高通去基线漂移（fc≈0.5Hz）
 *   2) 一阶低通（fc≈5Hz）
 *   3) 50Hz 二阶陷波（biquad，系数见下）
 *   4) N 点滑动平均
 * 输出 clean（已去直流+去噪的 AC）同时用于：屏上显示映射 + 峰值检测。
 *
 * 峰值检测：动态阈值 = 近期峰幅度*0.5；最小峰距 = 250ms（最高 240bpm）；
 *   越过阈值的局部最大且峰距合法 → 记一拍，由峰距算 PR（EMA 平滑）。
 * SQI：近 8 拍"峰-谷幅度(prominence)"的一致性 —— (1-(max-min)/(2*mean))*100。
 * ============================================================ */
#define PPG_FS_HZ           121.0f
#define PPG_DC_A            0.026f   /* 高通 α：fc≈2π*0.5/121≈0.026 → 去基线漂移 */
#define PPG_LP_A            0.26f    /* 低通 α：fc≈2π*5/121≈0.26 → 带通上限≈5Hz */
#define PPG_MA_N            4u       /* 滑动平均窗口（2 的幂）：≈33ms @121Hz */
#define PPG_MIN_PEAK_SAMPLES 30u     /* 250ms@121Hz：最高 240bpm，剔除噪声双峰 */
#define PPG_SQI_BEATS       8u       /* SQI 统计的近期拍数（2 的幂） */
#define PPG_PR_TIMEOUT_SAMPLES (3u * 121u)  /* 超 3s 无新峰 → PR 失效 */

/* 50Hz 陷波 biquad 系数（float，已对 a0 归一化；Fs=121, Q=30, f0=50）
 *   w0=2π*50/121, alpha=sin(w0)/(2Q)；b0=b2=1/a0, b1=a1=-2cos(w0)/a0, a2=(1-alpha)/a0 */
static const float PPG_NB0 = 0.99143f;
static const float PPG_NB1 = 1.69398f;
static const float PPG_NB2 = 0.99143f;
static const float PPG_NA1 = 1.69398f;
static const float PPG_NA2 = 0.98287f;

static struct {
    uint8_t  primed;
    float    dc;          /* 慢基线（去直流/漂移） */
    float    lp;          /* 低通输出 */
    float    nx1, nx2;    /* notch 输入延迟 */
    float    ny1, ny2;    /* notch 输出延迟 */
    float    ma_buf[PPG_MA_N];
    float    ma_sum;
    uint8_t  ma_i;

    /* 峰值检测 */
    uint32_t sample_cnt;
    uint32_t last_peak_idx;
    float    recent_amp;  /* 动态阈值用的近期峰幅度 */
    float    run_min;     /* 当前峰间谷值 */
    float    cand_val;    /* 候选峰幅度 */
    uint32_t cand_idx;    /* 候选峰序号 */
    uint8_t  above;       /* 是否处于超阈值状态 */
    int16_t  pr_est;
    uint8_t  pr_ok;

    /* SQI */
    float    prom[PPG_SQI_BEATS];
    uint8_t  prom_i;
    uint8_t  prom_cnt;
} s_ppg;

/* 复位滤波 + 峰值 + SQI（探头断开 / 初始化 / 长期暂停后调用） */
static void ppg_reset(void)
{
    uint8_t i;
    s_ppg.primed        = 0;
    s_ppg.dc            = 0.0f;
    s_ppg.lp            = 0.0f;
    s_ppg.nx1 = s_ppg.nx2 = 0.0f;
    s_ppg.ny1 = s_ppg.ny2 = 0.0f;
    s_ppg.ma_sum        = 0.0f;
    s_ppg.ma_i          = 0;
    for (i = 0; i < (uint8_t)PPG_MA_N; i++) s_ppg.ma_buf[i] = 0.0f;

    s_ppg.sample_cnt     = 0;
    s_ppg.last_peak_idx  = 0;
    s_ppg.recent_amp     = 0.0f;
    s_ppg.run_min        = 0.0f;
    s_ppg.cand_val       = 0.0f;
    s_ppg.cand_idx       = 0;
    s_ppg.above          = 0;
    s_ppg.pr_est         = 0;
    s_ppg.pr_ok          = 0;
    for (i = 0; i < (uint8_t)PPG_SQI_BEATS; i++) s_ppg.prom[i] = 0.0f;
    s_ppg.prom_i         = 0;
    s_ppg.prom_cnt       = 0;
}

/* 数字滤波链：原始 IR -> clean（已去直流+去噪的 AC）。仅在 valid 时调用。 */
static float ppg_filter(float raw)
{
    float ac, y;
    uint8_t i;

    if (!s_ppg.primed) { s_ppg.dc = raw; s_ppg.primed = 1; }

    /* 1) 慢 EMA 高通：去基线/漂移 */
    s_ppg.dc += (raw - s_ppg.dc) * PPG_DC_A;
    ac = raw - s_ppg.dc;

    /* 2) 一阶低通 ~5Hz */
    s_ppg.lp += (ac - s_ppg.lp) * PPG_LP_A;

    /* 3) 50Hz 陷波 biquad（Direct Form I） */
    y = PPG_NB0 * s_ppg.lp + PPG_NB1 * s_ppg.nx1 + PPG_NB2 * s_ppg.nx2
      - PPG_NA1 * s_ppg.ny1 - PPG_NA2 * s_ppg.ny2;
    s_ppg.nx2 = s_ppg.nx1; s_ppg.nx1 = s_ppg.lp;
    s_ppg.ny2 = s_ppg.ny1; s_ppg.ny1 = y;

    /* 4) 滑动平均 */
    s_ppg.ma_sum += y - s_ppg.ma_buf[s_ppg.ma_i];
    s_ppg.ma_buf[s_ppg.ma_i] = y;
    i = s_ppg.ma_i;
    s_ppg.ma_i = (uint8_t)((i + 1u) & (PPG_MA_N - 1u));

    return s_ppg.ma_sum / (float)PPG_MA_N;
}

/* 峰值检测 + PR/SQI 更新（输入为滤波后的 AC 信号 clean） */
static void ppg_peak(float clean)
{
    float a, thr, prom;

    /* 近期幅度（衰减最大值）→ 动态阈值 */
    a = (clean < 0.0f) ? -clean : clean;
    if (a > s_ppg.recent_amp) s_ppg.recent_amp = a;
    else s_ppg.recent_amp *= 0.9995f;

    if (clean < s_ppg.run_min) s_ppg.run_min = clean;

    thr = s_ppg.recent_amp * 0.5f;

    if (!s_ppg.above) {
        if (clean > thr) {
            s_ppg.above    = 1;
            s_ppg.cand_val = clean;
            s_ppg.cand_idx = s_ppg.sample_cnt;
        }
    } else {
        if (clean > s_ppg.cand_val) {
            s_ppg.cand_val = clean;
            s_ppg.cand_idx = s_ppg.sample_cnt;
        }
        if (clean < thr) {
            /* 下降穿越 → 确认一个峰 */
            uint32_t dpk = (s_ppg.cand_idx > s_ppg.last_peak_idx)
                         ? (s_ppg.cand_idx - s_ppg.last_peak_idx) : 0u;
            if (dpk >= PPG_MIN_PEAK_SAMPLES) {
                if (s_ppg.last_peak_idx != 0u) {
                    float interval = (float)dpk / PPG_FS_HZ;
                    float pr = 60.0f / interval;
                    if (pr >= 30.0f && pr <= 240.0f) {
                        if (!s_ppg.pr_ok) {
                            s_ppg.pr_est = (int16_t)pr; s_ppg.pr_ok = 1;
                        } else {
                            s_ppg.pr_est = (int16_t)((float)s_ppg.pr_est * 0.7f + pr * 0.3f);
                        }
                        /* SQI：本拍 prominence = 候选峰 - 谷值 */
                        prom = s_ppg.cand_val - s_ppg.run_min;
                        s_ppg.prom[s_ppg.prom_i] = prom;
                        s_ppg.prom_i = (uint8_t)((s_ppg.prom_i + 1u) & (PPG_SQI_BEATS - 1u));
                        if (s_ppg.prom_cnt < (uint8_t)PPG_SQI_BEATS) s_ppg.prom_cnt++;
                    }
                }
                s_ppg.last_peak_idx = s_ppg.cand_idx;
            }
            s_ppg.above   = 0;
            s_ppg.run_min = clean;   /* 重置谷值跟踪 */
        }
    }

    s_ppg.sample_cnt++;

    /* 超时：超过 3s 没新峰 → 视为信号丢失，PR 失效（避免冻结旧值） */
    if (s_ppg.pr_ok &&
        (s_ppg.sample_cnt - s_ppg.last_peak_idx) > PPG_PR_TIMEOUT_SAMPLES) {
        s_ppg.pr_ok = 0;
    }
}

/* 读取 F407 端用 IR 波形重算的 PR（0 = 尚未算出/信号丢失） */
int16_t HMI_WaveGetPR(void)
{
    return s_ppg.pr_ok ? s_ppg.pr_est : 0;
}

/* 读取真实 SQI 0~100（峰-峰一致性）；样本不足返回 0 */
uint8_t HMI_WaveGetSQI(void)
{
    float sum, mean, mx, mn, consist;
    uint8_t i;
    if (s_ppg.prom_cnt < 4u) return 0;
    sum = 0.0f; mx = -1.0e30f; mn = 1.0e30f;
    for (i = 0; i < s_ppg.prom_cnt; i++) {
        float v = s_ppg.prom[i];
        sum += v;
        if (v > mx) mx = v;
        if (v < mn) mn = v;
    }
    mean = sum / (float)s_ppg.prom_cnt;
    if (mean <= 0.0f) return 0;
    consist = 1.0f - (mx - mn) / (2.0f * mean);   /* 1 = 完全一致 */
    if (consist < 0.0f) consist = 0.0f;
    if (consist > 1.0f) consist = 1.0f;
    return (uint8_t)(consist * 100.0f);
}

/* 只清"归一化状态"（基线与包络），不动已攒在 pts[] 里的点 */
static void wave_reset_state(void)
{
    s_wave.dc     = 0;
    s_wave.hi     = 0;
    s_wave.lo     = 0;
    s_wave.primed = 0;
}

void HMI_WaveReset(void)
{
    s_wave.cnt = 0;
    wave_reset_state();
    ppg_reset();          /* 同步复位滤波/峰值/SQI 状态 */
}

/* 单点映射：原始 ADC -> 0~255 */
static uint8_t wave_map(int32_t raw)
{
    int32_t ac, span, floor_span, v;

    /* 0) 防御性限幅：UNO 帧若发生位翻转，ir_raw 可能是任意 int32 值。
     *    不限制的话 dc/ac 可能溢出 int32，并把包络带偏好几秒。
     *    AFE4490 输出为 22bit 二补码，量程 ±(2^21-1)，此处按 ±2^21 夹紧。 */
    if (raw >  AFE_RAW_LIMIT) raw =  AFE_RAW_LIMIT;
    if (raw < -AFE_RAW_LIMIT) raw = -AFE_RAW_LIMIT;

    if (!s_wave.primed) {
        /* 第一个采样直接当基线。否则 dc=0 而 raw 很大，ac 出现巨大瞬态，
         * 把包络顶到天上，波形会被压扁好几秒。 */
        s_wave.dc     = raw;
        s_wave.hi     = 0;
        s_wave.lo     = 0;
        s_wave.primed = 1;
    }

    /* 1) 慢 EMA 跟踪基线（去直流 / 缓慢漂移） */
    s_wave.dc += (raw - s_wave.dc) >> WAVE_DC_SHIFT;

    /* 2) 交流分量 + 上下包络（上升沿立即跟随，回落缓慢） */
    ac = raw - s_wave.dc;
    if (ac > s_wave.hi) s_wave.hi = ac;
    else                s_wave.hi -= (s_wave.hi - ac) >> WAVE_ENV_SHIFT;  /* 差值恒正 */
    if (ac < s_wave.lo) s_wave.lo = ac;
    else                s_wave.lo += (ac - s_wave.lo) >> WAVE_ENV_SHIFT;  /* 差值恒正 */

    span = s_wave.hi - s_wave.lo;
    if (span <= 0) return (uint8_t)WAVE_BASE;

    /* 3) 增益封顶：跨度太小（弱灌注 / 无脉搏 / 只剩噪声）时不无限放大。
     *    用 DC 的相对比例做阈值，与光照强度、增益、右移无关。 */
    floor_span = s_wave.dc / WAVE_MIN_SPAN_DIV;
    if (floor_span < WAVE_MIN_SPAN_ABS) floor_span = WAVE_MIN_SPAN_ABS;
    if (span < floor_span) span = floor_span;

    /* 4) 线性映射到 [BASE, TOP]：ac==lo 时落在基线，ac==hi 时到峰值 */
    v = WAVE_BASE + ((ac - s_wave.lo) * WAVE_RANGE) / span;
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

void HMI_WavePush(int32_t ir_raw, uint8_t valid)
{
    float clean;

    if (!valid) {
        /* 无有效采样（探头未接入 / 算法还没出值）：
         * 推一条平线贴底，并复位归一化状态，等有效后重新建立基线。
         * 这样探头取下时是一条干净的基线，而不是把噪声放大成乱抖的线。
         * 注：只复位基线与包络，不丢弃 pts[] 里已经攒好的点。 */
        if (s_wave.primed) wave_reset_state();
        ppg_reset();          /* 复位滤波/峰值/SQI，避免恢复瞬间乱跳 */
        s_wave.pts[s_wave.cnt++] = (uint8_t)WAVE_BASE;
        if (s_wave.cnt >= HMI_WAVE_CHUNK) HMI_WaveFlush();
        return;
    }

    /* ★ 2026-10-13：先经数字滤波链（去直流+5Hz 低通+50Hz 陷波+滑动平均），
     *   再用 clean 做显示映射，并同时跑峰值检测（PR/SQI）。屏上波形与 PR 同源。 */
    clean = ppg_filter((float)ir_raw);
    ppg_peak(clean);
    s_wave.pts[s_wave.cnt++] = wave_map((int32_t)clean);
    if (s_wave.cnt >= HMI_WAVE_CHUNK) HMI_WaveFlush();
}

void HMI_WaveFlush(void)
{
    if (s_wave.cnt == 0) return;
    HMI_GraphAddData(SCR_MAIN, MAIN_GRAPH_PPG, 0, s_wave.pts, s_wave.cnt);
    s_wave.cnt = 0;
}

/* ============================================================
 * 8. 上行通知（屏 -> MCU）
 *    帧: EE B1 <ctrl_msg> <screen:16> <ctrl:16> <type:8> <param...> FF FC FF FF
 *    ctrl_msg: 0x01 画面切换通知 / 0x11 控件数据通知
 *    type    : 0x10 按钮 0x11 文本 0x12 进度条 0x13 滑块 0x14 仪表 0x18 曲线
 * ============================================================ */
#define NOTIFY_CONTROL       0xB1
#define MSG_GET_SCREEN       0x01
#define MSG_GET_DATA         0x11
#define CTRL_BUTTON          0x10
#define CTRL_TEXT            0x11
#define CTRL_PROGRESS        0x12
#define CTRL_SLIDER          0x13

#define RX_BUF_SIZE          256

static uint8_t  s_rx[RX_BUF_SIZE];
static uint16_t s_rxlen;
static uint8_t  s_inframe;

/* 自屏累计收到的字节数（每收到 1 字节 +1），用于判定链路/波特率是否通 */
volatile uint32_t g_hmi_rx_bytes = 0;

void HMI_RxByte(uint8_t b)
{
    g_hmi_rx_bytes++;   /* 只要屏回了任何字节就累加 */

    /* 只看 EE 起头、FF FC FF FF 收尾的帧；中断里只做收字节 */
    if (!s_inframe) {
        if (b == 0xEE) { s_inframe = 1; s_rxlen = 0; }
        else return;
    }
    if (s_rxlen < RX_BUF_SIZE) s_rx[s_rxlen++] = b;

    if (s_rxlen >= 5 &&
        s_rx[s_rxlen - 4] == 0xFF && s_rx[s_rxlen - 3] == 0xFC &&
        s_rx[s_rxlen - 2] == 0xFF && s_rx[s_rxlen - 1] == 0xFF) {
        s_inframe = 0;   /* 等 HMI_Poll 取走 */
    }
}

#define U16BE(p)  ((uint16_t)(((p)[0] << 8) | (p)[1]))
#define U32BE(p)  (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | \
                   ((uint32_t)(p)[2] << 8)  |  (uint32_t)(p)[3])

void HMI_Poll(void)
{
    uint8_t cmd_type, ctrl_msg;
    uint16_t screen_id, control_id, plen;
    const uint8_t *p;

    if (s_inframe) return;                 /* 帧没收全 */
    /* 最短的有用上行帧 = 画面切换通知 EE B1 01 <screen:16> FF FC FF FF = 9 字节。
       ★ 2026-10-10 修正：原门限写的是 11，会把 9 字节的画面切换通知全部丢掉，
         导致 NotifyScreen() 永远不会被调用（"切页后补发文案"机制形同虚设）。
       按钮通知帧是 14 字节（2 字节 param），不受影响。 */
    if (s_rxlen < 9) { s_rxlen = 0; return; }

    if (s_rx[s_rxlen - 4] != 0xFF || s_rx[s_rxlen - 3] != 0xFC ||
        s_rx[s_rxlen - 2] != 0xFF || s_rx[s_rxlen - 1] != 0xFF) {
        s_rxlen = 0; return;
    }

    /* 屏 RTC 上报：EE F7 Year Mon Week Day Hour Min Sec FF FC FF FF（BCD），共 13 字节 */
    if (s_rx[1] == 0xF7) {
        if (s_rxlen >= 13) {
            NotifyRTC(bcd2bin(s_rx[2]),   /* 年(2 位) */
                      bcd2bin(s_rx[3]),   /* 月 */
                      bcd2bin(s_rx[5]),   /* 日 */
                      bcd2bin(s_rx[6]),   /* 时 */
                      bcd2bin(s_rx[7]),   /* 分 */
                      bcd2bin(s_rx[8]),   /* 秒 */
                      bcd2bin(s_rx[4]));  /* 星期 */
        }
        s_rxlen = 0; return;
    }

    cmd_type   = s_rx[1];
    ctrl_msg   = s_rx[2];
    screen_id  = U16BE(s_rx + 3);
    control_id = U16BE(s_rx + 5);
    p          = s_rx + 8;                 /* control_type 之后是参数 */
    plen       = (uint16_t)(s_rxlen - 4 - 8);

    if (cmd_type == NOTIFY_CONTROL) {
        uint8_t ctype = s_rx[7];
        if (ctrl_msg == MSG_GET_SCREEN) {
            NotifyScreen(screen_id);
        } else if (ctrl_msg == MSG_GET_DATA) {
            switch (ctype) {
            case CTRL_BUTTON:
                /* 按钮：param[0] 保留，param[1] 为状态（参考例程 NotifyButton 取 param[1]） */
                NotifyButton(screen_id, control_id, plen > 1 ? p[1] : 0);
                break;
            case CTRL_TEXT:
                NotifyText(screen_id, control_id, p, plen);
                break;
            case CTRL_PROGRESS:
                NotifyProgress(screen_id, control_id, U32BE(p));
                break;
            case CTRL_SLIDER:
                NotifySlider(screen_id, control_id, U32BE(p));
                break;
            default:
                break;
            }
        }
    }
    s_rxlen = 0;
}

/* ---------------- 初始化（屏无需握手；USART2 已在 usart.c 初始化） ---------------- */
void HMI_Init(void)
{
    /* 留空：大彩屏上电即就绪，无需 MCU 主动握手。
       如需开屏时清屏或使能趋势，可在这里发指令。 */
}

/* ---------------- 弱回调，应用层可重新实现 ---------------- */
__weak void NotifyScreen(uint16_t screen_id)          { (void)screen_id; }
__weak void NotifyButton(uint16_t screen_id, uint16_t ctrl_id, uint8_t state)
{ (void)screen_id; (void)ctrl_id; (void)state; }
__weak void NotifyProgress(uint16_t screen_id, uint16_t ctrl_id, uint32_t value)
{ (void)screen_id; (void)ctrl_id; (void)value; }
__weak void NotifySlider(uint16_t screen_id, uint16_t ctrl_id, uint32_t value)
{ (void)screen_id; (void)ctrl_id; (void)value; }
__weak void NotifyText(uint16_t screen_id, uint16_t ctrl_id, const uint8_t *str, uint16_t len)
{ (void)screen_id; (void)ctrl_id; (void)str; (void)len; }
__weak void NotifyRTC(uint8_t year2, uint8_t mon, uint8_t day,
                      uint8_t hour, uint8_t min, uint8_t sec, uint8_t week)
{ (void)year2; (void)mon; (void)day; (void)hour; (void)min; (void)sec; (void)week; }
