/*!
 * \file app_bloodoxygen.c
 * \brief 血氧屏应用层：把 UNO 解析出的测量数据写到 SPO2_Hospital 屏上
 *
 * 依赖：
 *   dc_hmi.c   屏驱动 / HMI_* 指令 / HMI_Wave* 峰检（PR 与信号质量来自 F407 端峰检）
 *   uno.c      USART6 的 AFE4490(代号 UNO) 血氧模块解析
 *   hmi_ids.h  控件号
 *   merge_cfg.h g_rtc_epoch_tick：两屏共用的软 RTC 基准
 * 硬件通道：
 *   USART2 -> 血氧屏 DC 串口屏(115200)
 *   USART6 -> AFE4490 血氧模块(57600)
 *
 * 时序（全部由主循环调用，非中断上下文，可重入性由调用方保证串行）：
 *   on_uno_frame()   收到 UNO 帧时推波形，约 90 帧/s
 *   task_hmi_value() 每 400ms  写数值/状态 + 报警判定
 *   task_hmi_trend() 每 2s     追加一点到历史曲线
 *   task_clock()     每 1s     推进软 RTC + 累加监护时长并刷"监护时间"
 *
 * 控件归属：屏端 main.lua 已停用（换成空壳脚本，原版备份为 main.lua.原版备份.txt），
 *   全部动态内容由 MCU 独占写入，控件号见 hmi_ids.h。
 * 画面跳转：button_type=0 的按钮用 switch 属性由屏原生跳页；需要 MCU 干活的按钮为
 *   button_type=1 + switch=""，只上报事件，跳页由 MCU 收到通知后发 EE B1 00 完成。
 * 中文字符串：AC5 不允许字符串字面量含非 ASCII（#870-D），屏上要显示的汉字一律用
 *   GBK 的 \xNN 转义，见下面 S_* 宏。
 */

#include "dc_hmi.h"
#include "hmi_ids.h"
#include "uno.h"
#include "sys.h"        /* millis() */
#include "app_bloodoxygen.h"
#include "merge_cfg.h"  /* g_rtc_epoch_tick：两屏共用的软 RTC 基准 */
#include "screen_watch.h" /* 屏在线探测与重握手：SCREEN_ID_SPO2 / screen_watch_touch */
#include "debug.h"        /* dbg_*：SQI 图标调试用 */
#include <stdio.h>
#include <stdlib.h>     /* atoi */
#include <string.h>

extern measurement_t g_meas;
/* 波形门控不用 g_new_frame：改用 uno.c 的采样环形缓冲 UNO_WaveTake，
   单个布尔标志会把多帧合并成一点，造成波形卡顿。 */

/* ============================================================
 * 0. 屏上要显示的汉字（GBK 转义，AC5 下不能直接写中文）
 *    每个宏都以 GBK 双字节结尾，绝不紧跟十六进制数字，
 *    避免 AC5 的 \x 转义"贪心多吃一位"（如 "\xE4" "0" 必须拆开写）。
 * ============================================================ */
#define S_PROBE_OK    "\xCC\xBD\xCD\xB7\xD2\xD1\xBD\xD3\xC8\xEB"        /* 探头已接入 */
#define S_PROBE_NO    "\xCC\xBD\xCD\xB7\xCE\xB4\xBD\xD3\xC8\xEB"        /* 探头未接入 */
#define S_NORMAL      "\xD5\xFD\xB3\xA3"                               /* 正常 */
#define S_ALM_SPO2    "\xD1\xAA\xD1\xF5\xC6\xAB\xB5\xCD"               /* 血氧偏低 */
#define S_ALM_PR      "\xC2\xF6\xC2\xCA\xC6\xAB\xBF\xEC"               /* 脉率偏快 */
#define S_PFX_SPO2    "\xD1\xAA\xD1\xF5"                               /* 血氧 */
#define S_PFX_PR      "\xC2\xF6\xC2\xCA"                               /* 脉率 */
#define S_LV_HIGH     "\xB8\xDF"                                       /* 高 */
#define S_LV_MID      "\xD6\xD0"                                       /* 中 */
#define S_ST_NEW      "\xCE\xB4\xB4\xA6\xC0\xED"                       /* 未处理 */
#define S_NO_REC      "\xD4\xDD\xCE\xDE\xB1\xA8\xBE\xAF\xBC\xC7\xC2\xBC" /* 暂无报警记录 */

/* 趋势时段提示：记录时段 1 分钟 / 5 分钟 / 30 分钟
   （2 秒/点：30 点=1 分钟、150 点=5 分钟、900 点=30 分钟） */
#define S_TRD_1MIN    "\xBC\xC7\xC2\xBC\xCA\xB1\xB6\xCE 1 \xB7\xD6\xD6\xD3"   /* 记录时段 1 分钟 */
#define S_TRD_5MIN    "\xBC\xC7\xC2\xBC\xCA\xB1\xB6\xCE 5 \xB7\xD6\xD6\xD3"   /* 记录时段 5 分钟 */
#define S_TRD_30MIN   "\xBC\xC7\xC2\xBC\xCA\xB1\xB6\xCE 30 \xB7\xD6\xD6\xD3"  /* 记录时段 30 分钟 */

/* ---- 暂停/监护状态文案（由 MCU 写，屏端 Lua 已停用） ---- */
#define S_VALID_OK    "\xB2\xE2\xC1\xBF\xD3\xD0\xD0\xA7"               /* 测量有效 */
#define S_VALID_PAUSE "\xD2\xD1\xD4\xDD\xCD\xA3"                       /* 已暂停   */
#define S_CAP_ON      "\xC2\xF6\xB2\xAB\xB2\xA8\xB2\xC9\xBC\xAF\xD6\xD0" /* 脉搏波采集中 */
#define S_CAP_PAUSE   "\xD4\xDD\xCD\xA3\xD6\xD0"                       /* 暂停中   */
/* "监护时间"前缀，后面用 "%s %02u:%02u:%02u" 拼，故宏内不带空格与数字 */
#define S_DUR_PFX     "\xBC\xE0\xBB\xA4\xCA\xB1\xBC\xE4"               /* 监护时间 */
/* 报警限值前缀 */
#define S_LIM_SPO2    "\xB1\xA8\xBE\xAF\xCF\xDE\xD6\xB5"               /* 报警限值 */
#define S_LIM_PR      "\xB1\xA8\xBE\xAF\xB8\xDF\xCF\xDE"
#define S_STALE       "\xCA\xFD\xBE\xDD\xB3\xC2\xBE\xC9"       /* 数据陈旧 */

/* 时效标注阈值（NodeA_未完成任务清单 §2.2：spo2/hr 时效标注）。
 * 超过该时长无任何有效结果(SpO2 帧 或 PR)落地，即判"数据陈旧"，屏转 -- 不再伪装新值。
 * 正常时 UNO 帧与 PR 峰检均远快于此（秒级以内），4s 可靠区分"链路/算法静默"。 */
#define SPO2_STALE_MS  4000u               /* 报警高限 */
/* 暂停弹窗提示语 */
#define S_PAU_ASK     "\xB5\xB1\xC7\xB0\xBC\xE0\xBB\xA4\xD6\xD0\x2C\x20\xC8\xB7\xC8\xCF\xD4\xDD\xCD\xA3\xBC\xE0\xBB\xA4\x3F"
                                                                       /* 当前监护中, 确认暂停监护? */
#define S_PAU_ASK2    "\xB5\xB1\xC7\xB0\xD2\xD1\xD4\xDD\xCD\xA3\x2C\x20\xC8\xB7\xC8\xCF\xBB\xD6\xB8\xB4\xBC\xE0\xBB\xA4\x3F"
                                                                       /* 当前已暂停, 确认恢复监护? */
/* 设置页状态文案 */
#define S_SND_ON      "\xC9\xF9\xD2\xF4\xCC\xE1\xCA\xBE\xD2\xD1\xBF\xAA\xC6\xF4" /* 声音提示已开启 */
#define S_SND_OFF     "\xC9\xF9\xD2\xF4\xCC\xE1\xCA\xBE\xD2\xD1\xB9\xD8\xB1\xD5" /* 声音提示已关闭 */

/* ============================================================
 * 1. 全局状态
 * ============================================================ */
uint8_t g_paused = 0;                 /* 暂停监护（由屏通知翻转） */

/* 时效标注（NodeA_未完成任务清单 §2.2）：当前展示值是否已"陈旧"。
 *   仅当"未暂停 且 数值有效(result_valid=1)"时才计算年龄；暂停/无效不判陈旧。
 *   同时顺带刷新 g_meas.age_ms（供诊断/快照）。返回 1=陈旧。
 *   判定依据见 SPO2_STALE_MS：链路/算法静默超过阈值即视为陈旧。 */
static uint8_t spo2_is_stale(void)
{
    if (g_paused || !g_meas.result_valid) return 0u;
    uint32_t age = millis() - g_meas.result_time_ms;
    g_meas.age_ms = age;
    return (uint8_t)(age > SPO2_STALE_MS);
}

static uint8_t s_sound_on = 1;        /* 报警声音开关（设置页切换按钮） */
static uint8_t s_bright_pct = 80;     /* 背光百分比，取值 10~100（设置页滑块） */
static int16_t s_spo2_low = 90;       /* SpO2 报警下限，有效范围 70~99，可由设置页修改 */
static int16_t s_pr_high  = 120;      /* 脉率报警上限，有效范围 60~200，可由设置页修改 */

/* 监护时长（秒）：正常监护时每秒 +1，暂停时冻结 */
static uint32_t s_monitor_sec = 0;

/* 上一次写进屏的监护时长文本，只有整秒变化才发帧，省带宽 */
static uint32_t s_dur_shown = 0xFFFFFFFFu;

/* 上一次写进屏的报警限值，只有设置页改阈值后才重发 */
static int16_t  s_lim_spo2_shown = -1;
static int16_t  s_lim_pr_shown   = -1;

/* 上次下发过的"暂停界面"状态（0xFF=还没下发过） */
static uint8_t  s_pause_ui_shown = 0xFF;

/* ---- 软 RTC：MCU 自己维护年月日时分秒，上电后从 00:00:00 开始走 ----
   RTC_INIT_* 指定起始日期，对上真实日期改这三行即可。 */
#define RTC_INIT_YEAR   2026
#define RTC_INIT_MON    1
#define RTC_INIT_DAY    1

/* 两屏共用基准（merge_cfg.h 里 extern）。谁先初始化谁填。 */
uint32_t g_rtc_epoch_tick = 0u;

/* 屏 RTC 校准周期（毫秒）。
 * 两块屏都用屏内 RTC 控件（EE 81 驱动），为使两屏"秒跳变相位"一致，这里每秒重发一次：
 *   一帧 EE 81 = 13 字节（本屏不开 CRC）≈ 1.1ms @115200 ⇒ 占用 ≈0.1%；
 *   每次重发都把屏内 RTC 对齐到 MCU 当前秒，屏的跳秒时刻锚定在"收到指令后 1 秒"，
 *   与血压屏（同一时刻发出的同一指令）保持同源。
 * 调大此值可减轻串口负担，代价是两屏可能各自漂出最多几十毫秒的相位差。 */
#define SPO2_RTC_SYNC_MS     1000u

/* 血氧屏 RTC 补偿（单位：秒，可正可负，默认 0 不补偿）。
   用法：把调试口打出的 MCU 时间与屏上时间逐秒对照——
     屏比 MCU 快 n 秒  -> 填 -n（让屏少走 n 秒）
     屏比 MCU 慢 n 秒  -> 填 +n
   只影响写进屏的时间戳，不影响 MCU 内部走时（监护时长/报警时间不变）。
   两屏同源后一般不需要它；若血压屏固定差 1 秒而血氧屏一致，问题在血压屏侧，勿用此宏补。 */
#define SPO2_RTC_OFFSET_SEC  0

static uint16_t s_year = RTC_INIT_YEAR;
static uint8_t  s_mon  = RTC_INIT_MON;
static uint8_t  s_day  = RTC_INIT_DAY;
static uint32_t s_sec_of_day = 0;          /* 当天已过秒数，0~86399 */
static uint32_t s_rtc_last_tick = 0;       /* 上次推进用的 millis() 基准（保留余数） */
static uint32_t s_rtc_last_sync = 0;       /* 上次同步到屏的时刻 */
static uint32_t s_pr_last_valid_ms = 0;    /* 上次 F407 PR 有效时的 millis() */
#if defined(DEBUG_PPG) && DEBUG_PPG
volatile uint16_t g_pr_loss_streak = 0;    /* 诊断：连续丢 PR 的 on_uno_frame 次数 */
volatile uint16_t g_uno_last_n = 0;        /* 诊断：上次 on_uno_frame 取到的采样数 */
volatile int16_t  g_last_pr_write = 0;       /* 诊断：上次 on_uno_frame 实际写进 g_meas.pulse_rate 的值 */
volatile uint8_t  g_last_pr_ok = 0;          /* 诊断：上次调用时 HMI_WaveGetPRDetail 的 ok */
volatile int16_t  g_last_pr_est = 0;         /* 诊断：上次调用时 HMI_WaveGetPRDetail 的 est */
volatile uint8_t  g_last_pr_valid = 0;       /* 诊断：上次调用时 valid 标志 */
#endif /* DEBUG_PPG */

/* ---- 报警记录 ---- */
#define ALM_ROWS      4u
#define ALM_HOLD_MS   3000u     /* 越限持续多久才记一条，防抖动 */
#define ALM_REL_SPO2  92        /* 解除阈值（滞回）：>= 92 才解除 */
#define ALM_REL_PR    115       /* 解除阈值（滞回）：<= 115 才解除 */

typedef struct {
    char tm[9];                 /* "HH:MM:SS" */
    char ev[26];                /* 事件，如 "血氧 88%" */
    char lv[8];                 /* 级别：高 / 中 */
    char st[16];                /* 状态：未处理 */
} alm_rec_t;

static alm_rec_t s_recs[ALM_ROWS];      /* [0] 为最新 */
static uint8_t   s_rec_cnt = 0;         /* 当前有效记录条数，0~ALM_ROWS */
static uint16_t  s_event_total = 0;     /* 累计报警事件数（显示在记录页条数控件） */

/* [0]=SpO2 [1]=脉率 */
static uint8_t  s_alm_active[2];        /* 该通道是否已处于报警态 */
static uint8_t  s_alm_pending_on[2];    /* 该通道是否正在计时等待确认越限 */
static uint32_t s_alm_pending_at[2];    /* 该通道越限计时的起点 millis() */

/* 探头状态条上次写过的值（0xFF = 还没写过），只在变化时写，省 7 帧带宽 */
static uint8_t  s_probe_shown = 0xFF;

/* ============================================================
 * 1.5 屏文案刷新（这些文案全部由 MCU 写，屏端 Lua 已停用）
 * ============================================================ */

/* 主界面右下角"监护时间 HH:MM:SS"。
   force=0 时只在整秒变了才发帧（每秒调用 → 正常情况每秒 1 帧）。 */
static void hmi_show_duration(uint8_t force)
{
    char b[32];

    if (!force && s_dur_shown == s_monitor_sec) return;
    s_dur_shown = s_monitor_sec;

    snprintf(b, sizeof(b), "%s %02u:%02u:%02u", S_DUR_PFX,
             (unsigned)(s_monitor_sec / 3600u),
             (unsigned)((s_monitor_sec / 60u) % 60u),
             (unsigned)(s_monitor_sec % 60u));
    HMI_SetText(SCR_MAIN, MAIN_TXT_DURATION, b);
}

/* 暂停/恢复界面：按钮显隐 + 两句状态文案 + 弹窗提示语。
   暂停 -> 藏起"暂停监护"(1)、亮出"继续监护"(6)；恢复 -> 反过来。
   force=1 用于刚切回主界面（屏已按默认属性重绘）之后强制重发一次。 */
static void hmi_show_pause_ui(uint8_t force)
{
    if (!force && s_pause_ui_shown == g_paused) return;
    s_pause_ui_shown = g_paused;

    if (g_paused) {
        HMI_SetControlVisible(SCR_MAIN, MAIN_BTN_PAUSE,  0u);
        HMI_SetControlVisible(SCR_MAIN, MAIN_BTN_RESUME, 1u);
        HMI_SetText(SCR_MAIN, MAIN_TXT_VALID,  S_VALID_PAUSE);   /* 已暂停   */
        HMI_SetText(SCR_MAIN, MAIN_TXT_CAP_ST, S_CAP_PAUSE);     /* 暂停中   */
    } else {
        HMI_SetControlVisible(SCR_MAIN, MAIN_BTN_RESUME, 0u);
        HMI_SetControlVisible(SCR_MAIN, MAIN_BTN_PAUSE,  1u);
        HMI_SetText(SCR_MAIN, MAIN_TXT_VALID,  S_VALID_OK);      /* 测量有效 */
        HMI_SetText(SCR_MAIN, MAIN_TXT_CAP_ST, S_CAP_ON);        /* 脉搏波采集中 */
    }

    /* 暂停弹窗里的提示语按当前状态给，下次点开就是对的 */
    HMI_SetText(SCR_PAUSE, PAU_TXT_CONTEXT, g_paused ? S_PAU_ASK2 : S_PAU_ASK);

    hmi_show_duration(1u);      /* 暂停/恢复瞬间时长文案也同步一次 */
}

/* 主界面报警限值文案：只有设置页改过阈值才重发 */
static void hmi_show_limits(uint8_t force)
{
    char b[32];

    if (!force && s_lim_spo2_shown == s_spo2_low && s_lim_pr_shown == s_pr_high) return;
    s_lim_spo2_shown = s_spo2_low;
    s_lim_pr_shown   = s_pr_high;

    snprintf(b, sizeof(b), "%s %d-100 %%", S_LIM_SPO2, (int)s_spo2_low);
    HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2_LIM, b);
    snprintf(b, sizeof(b), "%s %d bpm", S_LIM_PR, (int)s_pr_high);
    HMI_SetText(SCR_MAIN, MAIN_TXT_PR_LIM, b);
}

/* 设置页：亮度百分比 + 声音提示状态 + 进度条/滑块/阈值输入框回写。
   进度条/滑块/输入框的回写原先由屏端 main.lua 的 refresh_setup() 做，
   Lua 停用后改由 MCU 写，否则切到设置页时滑块位置与百分比文本会不一致。 */
static void hmi_show_setup(void)
{
    char b[16];

    snprintf(b, sizeof(b), "%d%%", (int)s_bright_pct);
    HMI_SetText(SCR_SETUP, SET_TXT_BRIGHT, b);
    HMI_SetText(SCR_SETUP, SET_TXT_SOUND_ST, s_sound_on ? S_SND_ON : S_SND_OFF);

    /* 进度条与滑块显示当前亮度（两者 ID 相邻：进度条 10 / 滑块 11） */
    HMI_SetProgress(SCR_SETUP, SET_BAR_BRIGHT,    (uint32_t)s_bright_pct);
    HMI_SetProgress(SCR_SETUP, SET_SLIDER_BRIGHT, (uint32_t)s_bright_pct);

    /* 报警阈值输入框回写（原先也是 Lua 写） */
    snprintf(b, sizeof(b), "%d", (int)s_spo2_low);
    HMI_SetText(SCR_SETUP, SET_TXT_SPO2_LOW, b);
    snprintf(b, sizeof(b), "%d", (int)s_pr_high);
    HMI_SetText(SCR_SETUP, SET_TXT_PR_HIGH, b);
}

/* ============================================================
 * 2. 软 RTC
 * ============================================================ */
static uint8_t rtc_weekday(uint16_t y, uint8_t m, uint8_t d)
{
    /* 0=周日 .. 6=周六（与屏 RTC 指令一致：星期天=0） */
    int yy = y, mm = m;
    int w;
    if (mm < 3) { mm += 12; yy -= 1; }
    w = d + 2 * mm + 3 * (mm + 1) / 5 + yy + yy / 4 - yy / 100 + yy / 400 + 1;
    return (uint8_t)(((w % 7) + 7) % 7);
}

/* 把 (y,m,d) 按 ±1 天平移。只用于 RTC 补偿在零点前后的借位/进位，
   且只作用于传进来的副本，不改动模块内的 s_year/s_mon/s_day 状态。 */
static void rtc_day_step(uint16_t *Y, uint8_t *M, uint8_t *D, int delta)
{
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint16_t y = *Y;
    uint8_t  m = *M, d = *D, feb, dmax;

    if (delta > 0) {
        feb  = ((y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u) ? 29u : 28u;
        dmax = (m == 2u) ? feb : dim[m - 1u];
        d++;
        if (d > dmax) { d = 1u; m++; if (m > 12u) { m = 1u; y++; } }
    } else if (delta < 0) {
        if (d > 1u) { d--; }
        else {
            if (m > 1u) { m--; } else { m = 12u; y--; }
            feb  = ((y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u) ? 29u : 28u;
            dmax = (m == 2u) ? feb : dim[m - 1u];
            d = dmax;
        }
    }
    *Y = y; *M = m; *D = d;
}

/* 组一帧"要写给屏的 RTC 时间"（已含 SPO2_RTC_OFFSET_SEC 补偿）。
   out 顺序：年(2位) / 月 / 日 / 时 / 分 / 秒 / 星期。 */
static void rtc_build_display(uint8_t *y2, uint8_t *mon, uint8_t *day,
                              uint8_t *hh, uint8_t *mi, uint8_t *ss, uint8_t *wk)
{
    uint16_t Y = s_year;
    uint8_t  M = s_mon, D = s_day;
    int32_t  sod = (int32_t)s_sec_of_day + (int32_t)SPO2_RTC_OFFSET_SEC;

    if (sod < 0)           { sod += 86400; rtc_day_step(&Y, &M, &D, -1); }
    else if (sod >= 86400) { sod -= 86400; rtc_day_step(&Y, &M, &D, +1); }

    *y2  = (uint8_t)(Y % 100u);
    *mon = M; *day = D;
    *hh  = (uint8_t)(sod / 3600);
    *mi  = (uint8_t)((sod / 60) % 60);
    *ss  = (uint8_t)(sod % 60);
    *wk  = rtc_weekday(Y, M, D);
}

/* 立即把当前软 RTC 同步到血氧屏（开机 / 用户改时间 / 周期性校准都用它） */
static void rtc_sync_to_screen(void)
{
    uint8_t y2, mon, day, hh, mi, ss, wk;
    rtc_build_display(&y2, &mon, &day, &hh, &mi, &ss, &wk);
    HMI_SetRTCTime(y2, mon, day, hh, mi, ss, wk);
}

/* 每秒调用：把 millis() 流逝的整秒数加到软 RTC 上（保留不足 1s 的余数，防累计误差） */
void task_clock(void)
{
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t now = millis();
    uint32_t add = (uint32_t)((now - s_rtc_last_tick) / 1000u);

    if (add == 0u) return;
    s_rtc_last_tick += add * 1000u;      /* 余数留在基准里，下次继续攒 */
    s_sec_of_day   += add;

    while (s_sec_of_day >= 86400u) {
        uint8_t feb = ((s_year % 4u == 0u && s_year % 100u != 0u) ||
                       s_year % 400u == 0u) ? 29u : 28u;
        uint8_t dmax = (s_mon == 2u) ? feb : dim[s_mon - 1u];
        s_sec_of_day -= 86400u;
        s_day++;
        if (s_day > dmax) {
            s_day = 1;
            s_mon++;
            if (s_mon > 12u) { s_mon = 1; s_year++; }
        }
    }

    /* 每 SPO2_RTC_SYNC_MS 把 MCU 时间同步给屏（屏 RTC 有独立晶振，此周期用于校准） */
    if ((now - s_rtc_last_sync) >= SPO2_RTC_SYNC_MS || s_rtc_last_sync == 0u) {
        s_rtc_last_sync = now;
        rtc_sync_to_screen();
    }

    /* ---- 监护时长：正常监护时随秒累加，暂停时冻结，写主界面 txt_duration(21) ---- */
    if (!g_paused) s_monitor_sec += add;
    hmi_show_duration(0u);
}

/* ---- 供别处读取/写入血氧侧软 RTC（诊断打印 / 血压页改时间后同步两屏） ---- */
void spo2_rtc_get(uint16_t *y, uint8_t *mon, uint8_t *day,
                  uint8_t *hh, uint8_t *mi, uint8_t *ss)
{
    if (y)   *y   = s_year;
    if (mon) *mon = s_mon;
    if (day) *day = s_day;
    if (hh)  *hh  = (uint8_t)(s_sec_of_day / 3600u);
    if (mi)  *mi  = (uint8_t)((s_sec_of_day / 60u) % 60u);
    if (ss)  *ss  = (uint8_t)(s_sec_of_day % 60u);
}

/* 把当前软 RTC 日历换算成 Unix epoch 秒（1970-01-01 00:00:00 UTC 起算），
 * 供 CANopen 对象 2100:01 上送"UTC epoch 秒"（见《CANopen_对象字典_v0.1》2100:01、
 * 《MPM_统一数据字典_v0.1》2100:01）。
 *   采用 MCU 内部真实走时(s_sec_of_day)，不含 SPO2_RTC_OFFSET_SEC 屏显补偿；
 *   闰年规则含 100/400 整百年修正（2100 非闰年），覆盖 1970~2105 全区间。
 *   返回 32 位值：2026-01-01 约为 1.767e9，远未溢出 u32（上限 ~4.29e9，约到 2106）。
 * Gateway 据此把本节点时间戳对齐到统一时基，与 2100:02 的 uwTick 单调节拍配合，
 * 既可得绝对时刻也能算相对时延。 */
uint32_t spo2_rtc_get_epoch(void)
{
    static const uint16_t month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t days = 0u;
    uint16_t y;
    uint8_t  m;
    int is_leap = ((s_year % 4u == 0u && s_year % 100u != 0u) || s_year % 400u == 0u) ? 1 : 0;

    /* 1970-01-01 到 (s_year-01-01) 的整年天数 */
    for (y = 1970; y < s_year; y++) {
        days += ((y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u) ? 366u : 365u;
    }
    /* 当年 1 月 .. (s_mon-1) 月天数 */
    for (m = 1u; m < s_mon; m++) {
        days += month_days[m - 1];
        if (m == 2u && is_leap) days += 1u;
    }
    days += (uint32_t)(s_day - 1u);
    return days * 86400u + s_sec_of_day;
}

/* 用户在血压屏"时间设置"页改完时间后，必须把血氧侧软 RTC 一起对齐，
   否则两块屏会各走各的（改一次就差几小时）。
   参数越界（年<2000、月>12、日>31、时>23、分/秒>59）时直接返回不改。 */
void spo2_rtc_set(uint16_t y, uint8_t mon, uint8_t day,
                  uint8_t hh, uint8_t mi, uint8_t ss)
{
    if (y < 2000u || mon < 1u || mon > 12u || day < 1u || day > 31u ||
        hh > 23u || mi > 59u || ss > 59u) return;
    s_year = y; s_mon = mon; s_day = day;
    s_sec_of_day = (uint32_t)hh * 3600u + (uint32_t)mi * 60u + (uint32_t)ss;
    /* 基准对齐到"本次改时间的瞬间"，不足 1 秒的余数丢掉即可 */
    s_rtc_last_tick = millis();
    s_rtc_last_sync = s_rtc_last_tick;
    rtc_sync_to_screen();          /* 立即写屏，不等周期校准 */
}

/* ============================================================
 * 3. 报警记录
 * ============================================================ */
/* 把 4 行 x 4 列 + 条数 + 空态 写进报警记录页（单条写，本屏不吃批量指令） */
static void alm_push_rows(void)
{
    char cnt[8];
    uint8_t r, c;

    for (r = 0; r < ALM_ROWS; r++) {
        const char *f[4];
        if (r < s_rec_cnt) {
            f[0] = s_recs[r].tm; f[1] = s_recs[r].ev;
            f[2] = s_recs[r].lv; f[3] = s_recs[r].st;
        } else {
            f[0] = f[1] = f[2] = f[3] = "";
        }
        for (c = 0; c < 4; c++) {
            /* 行 n(n=1..4) 的四个文本控件：时间=20+(n-1)*4, 事件+1, 级别+2, 状态+3 */
            HMI_SetText(SCR_ALARM, (uint16_t)(20u + r * 4u + c), f[c]);
        }
    }

    snprintf(cnt, sizeof(cnt), "%u", (unsigned)s_event_total);
    HMI_SetText(SCR_ALARM, ALM_TXT_EVENT_CNT, cnt);

    /* 空态提示"暂无报警记录"不能仅清空文本，否则背景色块仍遮住表格线。
       有记录时隐藏整个控件（文字+背景一起消失）；无记录时显示并写文字。 */
    if (s_rec_cnt == 0u) {
        HMI_SetText(SCR_ALARM, ALM_TXT_EMPTY, S_NO_REC);
        HMI_SetControlVisible(SCR_ALARM, ALM_TXT_EMPTY, 1u);
    } else {
        HMI_SetControlVisible(SCR_ALARM, ALM_TXT_EMPTY, 0u);
    }
}

/* 产生一条记录（插到最前，只保留 ALM_ROWS 条）：
   who: 0=SpO2 记"高"级别，1=脉率记"中"级别；val: 触发时的测量值 */
static void alm_emit(uint8_t who, int16_t val)
{
    char tm[9], ev[26];

    snprintf(tm, sizeof(tm), "%02u:%02u:%02u",
             (unsigned)(s_sec_of_day / 3600u),
             (unsigned)((s_sec_of_day / 60u) % 60u),
             (unsigned)(s_sec_of_day % 60u));
    if (who == 0u) snprintf(ev, sizeof(ev), "%s%d%%",   S_PFX_SPO2, (int)val);
    else           snprintf(ev, sizeof(ev), "%s%d bpm", S_PFX_PR,   (int)val);

    if (s_rec_cnt < ALM_ROWS) s_rec_cnt++;
    {
        uint8_t i = (uint8_t)(s_rec_cnt - 1u);
        while (i > 0u) { s_recs[i] = s_recs[i - 1u]; i--; }
    }
    strncpy(s_recs[0].tm, tm, sizeof(s_recs[0].tm) - 1u); s_recs[0].tm[sizeof(s_recs[0].tm) - 1u] = '\0';
    strncpy(s_recs[0].ev, ev, sizeof(s_recs[0].ev) - 1u); s_recs[0].ev[sizeof(s_recs[0].ev) - 1u] = '\0';
    strncpy(s_recs[0].lv, (who == 0u) ? S_LV_HIGH : S_LV_MID, sizeof(s_recs[0].lv) - 1u);
    s_recs[0].lv[sizeof(s_recs[0].lv) - 1u] = '\0';
    strncpy(s_recs[0].st, S_ST_NEW, sizeof(s_recs[0].st) - 1u);
    s_recs[0].st[sizeof(s_recs[0].st) - 1u] = '\0';

    s_event_total++;
    alm_push_rows();

    if (s_sound_on) HMI_Beep(200u);      /* 报警提示音（声音开关打开时） */
}

/* 单个通道的越限判定（滞回）：
   bad=1 且未在报警态时先计时，持续 ALM_HOLD_MS 才记一条；
   bad=0 且当前在报警态、且 rel_ok=1 时才解除。 */
static void alm_watch(uint8_t who, uint8_t bad, int16_t val, uint8_t rel_ok)
{
    uint32_t now = millis();

    if (bad) {
        if (s_alm_active[who]) return;
        if (!s_alm_pending_on[who]) {
            s_alm_pending_on[who] = 1u;
            s_alm_pending_at[who] = now;
        } else if ((now - s_alm_pending_at[who]) >= ALM_HOLD_MS) {
            s_alm_pending_on[who] = 0u;
            s_alm_active[who]     = 1u;
            alm_emit(who, val);
        }
    } else {
        s_alm_pending_on[who] = 0u;
        if (s_alm_active[who] && rel_ok) s_alm_active[who] = 0u;
    }
}

static void alm_update(void)
{
    uint8_t valid = g_meas.result_valid;

    /* 暂停监护期间不做报警判定（数值显示冻结值），状态文案回到"正常" */
    if (g_paused) {
        HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2_ST, S_NORMAL);
        HMI_SetText(SCR_MAIN, MAIN_TXT_PR_ST,   S_NORMAL);
        return;
    }

    /* 数据陈旧：数值虽"有效"但已长时间无新结果（链路/算法静默），
     * 无法确认是否仍超阈，释放已激活报警并提示"数据陈旧"（不写报警记录）。 */
    if (spo2_is_stale()) {
        HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2_ST, S_STALE);
        HMI_SetText(SCR_MAIN, MAIN_TXT_PR_ST,   S_STALE);
        s_alm_active[0] = 0u; s_alm_active[1] = 0u;
        s_alm_pending_on[0] = 0u; s_alm_pending_on[1] = 0u;
        return;
    }

    alm_watch(0u,
              (uint8_t)((valid && g_meas.spo2 < s_spo2_low) ? 1u : 0u),
              g_meas.spo2,
              (uint8_t)((valid && g_meas.spo2 >= ALM_REL_SPO2) ? 1u : 0u));

    alm_watch(1u,
              (uint8_t)((valid && g_meas.pulse_rate > s_pr_high) ? 1u : 0u),
              g_meas.pulse_rate,
              (uint8_t)((valid && g_meas.pulse_rate <= ALM_REL_PR) ? 1u : 0u));

    /* 主界面两个状态文本（Lua 不管这两个，由 MCU 写） */
    HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2_ST, s_alm_active[0] ? S_ALM_SPO2 : S_NORMAL);
    HMI_SetText(SCR_MAIN, MAIN_TXT_PR_ST,   s_alm_active[1] ? S_ALM_PR   : S_NORMAL);
}

static void alm_clear(void)
{
    s_rec_cnt = 0;
    s_event_total = 0;
    s_alm_active[0] = s_alm_active[1] = 0;
    s_alm_pending_on[0] = s_alm_pending_on[1] = 0;
    memset(s_recs, 0, sizeof(s_recs));
    alm_push_rows();
}

/* ============================================================
 * 4. 波形
 * ============================================================
 * uno.c 在中断里把每一个 IR 采样写入环形缓冲；这里每次把积压的采样全部取走并推平，
 * 而不是"收到一帧只推一个点"。主循环被别的任务（如血压屏 9600 波特率阻塞发送）
 * 占住时也不会丢点，波形横向采样比例恒定，不会出现"一秒卡一次、脉冲间距忽宽忽窄"。
 *
 * 调用时机：收到 UNO 帧时由上层调用，约 90 次/s；非中断上下文。 */
void on_uno_frame(void)
{
    int32_t  w[16];                  /* 单次最多取走的点数（栈上 64B；栈总容量仅 1KB） */
    uint16_t n, i;
    uint8_t  valid;

    if (g_paused) {                  /* 暂停：丢弃积压，避免恢复瞬间一次性冲出 */
        UNO_WaveClear();
        return;
    }

    n = UNO_WaveTake(w, (uint16_t)(sizeof(w) / sizeof(w[0])));
#if defined(DEBUG_PPG) && DEBUG_PPG
    g_uno_last_n = n;                /* 诊断：串口可观察本次取到几个点 */
#endif
    if (n == 0u) return;             /* 没新采样：直接返回，开销可忽略 */

    /* UNO 的 SpO2 算法可能短暂返回 0（pl[8]=0），但 IR 波形/HR 仍有效。
       若仅用 probe_state(Spo2!=0) 作 valid，会反复触发 ppg_reset 使 PR 掉 0。
       故取：SpO2 有效 或 UNO 原算 HR>0 时都认为 IR 波形可用，继续跑峰检。 */
    valid = (g_meas.probe_state != 0u) || (g_uno_hr > 0u);
    for (i = 0; i < n; i++) HMI_WavePush(w[i], valid);

    /* 用 F407 端 IR 波形重算的 PR 与真实 SQI 覆盖 UNO 值：PR 走峰值检测
       （动态阈值 + 最小峰距 250ms，噪声不会被计成 208），SQI 走峰-峰一致性。
       pr_ok 因抖动/边界短暂变 0 时，保持最后一次有效 PR，超过 3s 才回落到 0，
       避免屏上/串口 PR 跟着跳 0。 */
#if defined(DEBUG_PPG) && DEBUG_PPG
    g_last_pr_valid = valid;
#endif
    if (valid) {
        uint8_t  ok;
        int16_t  est;
        HMI_WaveGetPRDetail(&ok, &est, NULL);
#if defined(DEBUG_PPG) && DEBUG_PPG
        g_last_pr_ok  = ok;
        g_last_pr_est = est;
#endif
        /* 峰检偶发"假峰"（两峰过近/单峰被劈开）会吐出 226 这类生理不可能的 PR。
           加生理上限保护（<=220）挡掉假峰：超限帧当无效丢弃，g_meas.pulse_rate
           保持上次有效值，下一帧正常即自动恢复，不清零。 */
        if (ok && est > 0 && est <= 220) {
            g_meas.pulse_rate     = est;
            s_pr_last_valid_ms    = millis();
            g_meas.result_time_ms = millis();   /* 时效标注：新 PR 也刷新时间戳 */
#if defined(DEBUG_PPG) && DEBUG_PPG
            g_pr_loss_streak      = 0;
#endif
        } else {
#if defined(DEBUG_PPG) && DEBUG_PPG
            g_pr_loss_streak++;
#endif
            /* 短暂丢峰不立刻清零，避免屏上 PR 闪烁 */
            if ((int32_t)(millis() - s_pr_last_valid_ms) > 3000) {
                g_meas.pulse_rate = 0;
            }
        }
        g_meas.signal_quality = HMI_WaveGetSQI();
    }
#if defined(DEBUG_PPG) && DEBUG_PPG
    g_last_pr_write = g_meas.pulse_rate;
#endif
}

/* ============================================================
 * 5. 数值 + 状态刷新（每 400ms；由 task_hmi_value 调用，非中断上下文）
 *    暂停时数值显示冻结值（不再清成 --），状态文案与时长继续维持，
 *    避免屏上残留暂停前的旧数字或露出中断写入的 0。
 * ============================================================ */
static uint32_t t_val = 0;

/* 暂停时冻结显示的"最后一次有效"血氧/脉率：仅在"未暂停且数据有效"时刷新，
   g_paused 期间保持不变，使主界面（及趋势页最近值）在暂停时保留暂停前数值，
   恢复后再随实测数据变化。-1 表示从未有过有效值。 */
static int16_t s_frozen_spo2 = -1;
static int16_t s_frozen_pr  = -1;
static uint8_t s_frozen_sqi = 0;    /* 暂停时冻结显示的信号质量（最后一次有效值） */

/* 信号质量柱状图：最近一次实际写屏的 SQI 与"是否点亮"，供切页/自愈补发时重放 */
static uint8_t s_disp_sqi    = 0;
static uint8_t s_bar_active  = 0;

/* 切回某画面后屏会按控件默认属性重绘，需要把 MCU 负责的内容再补几次。
   s_resync_round>0 表示还要重发几轮，避免和屏重绘抢时序；
   另有周期性自愈补发（RESYNC_SELF_HEAL_MS），保证任何一次覆盖/丢失都能自动收敛。 */
static uint8_t  s_resync_round = 0;
static uint32_t s_resync_at    = 0;
static uint8_t  s_resync_alm   = 0;   /* 本轮补发是否连报警记录页一起补 */
static uint8_t  s_force_probe  = 0;   /* 强制下一轮把"探头已接入"整轮重写 */

#define RESYNC_SELF_HEAL_MS  3000u

/* 定义在第 6 节（趋势），这里先声明，供切页补发时调用 */
static void trend_show_labels(uint8_t force);

/* 信号质量 6 段 RAG 柱状图：主界面(SCR_MAIN) 与探头状态页(SCR_PROBE) 各有一套
 * 叠放的 image 控件（quality_0..5.png，控件 ID 50~55），颜色已烤进 PNG
 * （0=全暗/全灭 1=红 2=红 3=黄 4=绿 5=全绿）。MCU 用 HMI_SetControlVisible
 * 只显示当前档位那一张、隐藏其余；active=0 / SQI=0 时显示 quality_0(全暗=全灭)。
 * 本驱动无 HMI_SetIcon，故用"图标显隐"换色。另保留宽进度条 PRB_BAR_QUALITY
 * 给"量"（非激活时归零->空）。 */
static const uint16_t s_main_sqi_icon[6] = {
    MAIN_SQI_ICON0, MAIN_SQI_ICON1, MAIN_SQI_ICON2,
    MAIN_SQI_ICON3, MAIN_SQI_ICON4, MAIN_SQI_ICON5
};
static const uint16_t s_prb_sqi_icon[6] = {
    PRB_SQI_ICON0, PRB_SQI_ICON1, PRB_SQI_ICON2,
    PRB_SQI_ICON3, PRB_SQI_ICON4, PRB_SQI_ICON5
};

/* SQI(0~100) -> 图标档位(0~5)：0 特判为全灭；每 20% 一档 */
static uint8_t sqi_to_seg(uint8_t sqi)
{
    if (sqi == 0u) return 0u;
    if (sqi < 20u) return 1u;
    if (sqi < 40u) return 2u;
    if (sqi < 60u) return 3u;
    if (sqi < 80u) return 4u;
    return 5u;
}

/* 在某一画面上，只让 icons[level] 显示、其余 5 张隐藏 */
static void hmi_show_sqi_level(uint16_t screen, const uint16_t *icons, uint8_t level)
{
    uint8_t i;
    for (i = 0u; i < 6u; i++) {
        HMI_SetControlVisible(screen, icons[i], (i == level) ? 1u : 0u);
    }
}

static void hmi_update_sqi_bar(uint8_t sqi, uint8_t active)
{
    uint8_t level = active ? sqi_to_seg(sqi) : 0u;   /* 非激活 -> quality_0 全灭 */
    /* 调试用：只在 level/active 变化时打印一次，避免 400ms 刷屏 */
    {
        static uint8_t s_last_level = 0xFF;
        static uint8_t s_last_active = 0xFF;
        if (level != s_last_level || active != s_last_active) {
            s_last_level = level;
            s_last_active = active;
            dbg_puts("[SQI] active="); dbg_put_u32(active);
            dbg_puts(" sqi=");         dbg_put_u32(sqi);
            dbg_puts(" level=");        dbg_put_u32(level);
            dbg_puts(" main_icon=");    dbg_put_u32(s_main_sqi_icon[level]);
            dbg_puts("\r\n");
        }
    }
    hmi_show_sqi_level(SCR_MAIN,  s_main_sqi_icon, level);
    hmi_show_sqi_level(SCR_PROBE, s_prb_sqi_icon,  level);
    /* 兼容旧宽进度条（探头状态页）：仍按 SQI 写满量程；非激活归零 -> 空(全灭) */
    HMI_SetProgress(SCR_PROBE, PRB_BAR_QUALITY, active ? (uint32_t)sqi : 0u);
}

void task_hmi_value(void)
{
    static char s[3][24];
    static uint32_t s_heal_at = 0;
    uint16_t pg;
    uint32_t now = millis();

    /* (a) 切页后补发：约 +120/+420/+720/+1020ms 各一轮 */
    if (s_resync_round && (int32_t)(now - s_resync_at) >= 0) {
        if (--s_resync_round) s_resync_at = now + 300u;
        hmi_show_pause_ui(1u);
        hmi_show_limits(1u);
        hmi_show_setup();
        hmi_show_duration(1u);
        trend_show_labels(1u);       /* 趋势页最近值/时间刻度 */
        if (s_resync_alm) { alm_push_rows(); if (!s_resync_round) s_resync_alm = 0u; }
        hmi_update_sqi_bar(s_disp_sqi, s_bar_active);  /* 切页重绘会隐藏图标，补发重放 */
        s_force_probe = 1u;
    }

    /* (b) 周期自愈：不依赖任何通知，每 3 秒把 MCU 负责的文案整轮重写一遍 */
    if ((int32_t)(now - s_heal_at) >= 0) {
        s_heal_at = now + RESYNC_SELF_HEAL_MS;
        hmi_show_pause_ui(1u);
        hmi_show_limits(1u);
        hmi_show_setup();
        hmi_show_duration(1u);
        trend_show_labels(1u);       /* 趋势页最近值/时间刻度 */
        hmi_update_sqi_bar(s_disp_sqi, s_bar_active);  /* 自愈：重放柱状图 */
        s_force_probe = 1u;
    }

    if (now < t_val) return;
    t_val = now + 400u;

    /* 时效标注：先判定当前值是否陈旧（顺便刷新 g_meas.age_ms）。
     * 陈旧 = 数值仍有效但已超过 SPO2_STALE_MS 无新结果落地（链路/算法静默）。 */
    uint8_t stale = spo2_is_stale();
    uint8_t eff_valid = (uint8_t)(g_meas.result_valid && !stale);
    /* 信号质量"可显示"门控：必须探头已接入 且 数值有效(未陈旧)，
       仅 result_valid 不够——曾出现"探头未接入"却 SQI=85，故显式加 probe_state。 */
    uint8_t quality_active = (g_meas.probe_state != 0u) && eff_valid;

    /* 仅在"未暂停且数据有效(含未陈旧)"时刷新冻结缓存；暂停/无效/陈旧期间保持不变 */
    if (!g_paused && eff_valid) {
        s_frozen_spo2 = (int16_t)g_meas.spo2;
        s_frozen_pr  = (int16_t)g_meas.pulse_rate;
        s_frozen_sqi = g_meas.signal_quality;
    }

    uint8_t disp_sqi;
    uint8_t bar_active;

    if (g_paused) {
        /* 暂停：保留暂停前的最后一次有效值（不再显示 --） */
        if (s_frozen_spo2 >= 0) {
            snprintf(s[0], sizeof(s[0]), "%d", (int)s_frozen_spo2);
            snprintf(s[1], sizeof(s[1]), "%d", (int)s_frozen_pr);
            disp_sqi   = s_frozen_sqi;
            bar_active = 1u;          /* 冻结有效值：柱状图保留冻结质量 */
        } else {
            strcpy(s[0], "--");
            strcpy(s[1], "--");
            disp_sqi   = 0u;
            bar_active = 0u;          /* 从未有有效值：柱状图全灭 */
        }
        snprintf(s[2], sizeof(s[2]), "%d", (int)disp_sqi);
    } else if (!quality_active) {
        /* 探头未接入 / 无效 / 数据陈旧：一律显示 --，柱状图全灭（含 SQI=0） */
        strcpy(s[0], "--");
        strcpy(s[1], "--");
        strcpy(s[2], "--");
        disp_sqi   = 0u;
        bar_active = 0u;
    } else {
        snprintf(s[0], sizeof(s[0]), "%d", (int)g_meas.spo2);
        snprintf(s[1], sizeof(s[1]), "%d", (int)g_meas.pulse_rate);
        snprintf(s[2], sizeof(s[2]), "%d", (int)g_meas.signal_quality);
        disp_sqi   = g_meas.signal_quality;
        bar_active = 1u;
    }

    HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2,    s[0]);
    HMI_SetText(SCR_MAIN, MAIN_TXT_PR,      s[1]);
    HMI_SetText(SCR_MAIN, MAIN_TXT_QUALITY, s[2]);

    /* 探头状态页：5 段 RAG 柱状图（SQI=0 或 探头未接入 → 全灭）。
       兼容旧单进度条 PRB_BAR_QUALITY 仍按 SQI 写满量程；若 .tft 已删该控件屏会忽略。 */
    s_disp_sqi   = disp_sqi;
    s_bar_active = bar_active;
    hmi_update_sqi_bar(disp_sqi, bar_active);

    /* 报警限值文案（13/15）：设置页改过阈值才会重发 */
    hmi_show_limits(0u);

    /* 公共状态条的"探头已接入/未接入"：每页都有这个控件。
       状态变化时整轮刷一遍；补发/自愈轮次也强制整轮刷（切页重绘会把它冲掉）。 */
    {
        uint8_t st = g_meas.probe_state ? 1u : 0u;
        const char *txt = st ? S_PROBE_OK : S_PROBE_NO;
        HMI_SetText(SCR_PROBE, PRB_TXT_PROBE, txt);
        if (st != s_probe_shown || s_force_probe) {
            s_probe_shown  = st;
            s_force_probe  = 0u;
            for (pg = 0; pg <= (uint16_t)SCR_CLEAR; pg++) {
                HMI_SetText(pg, MAIN_TXT_PROBE, txt);
            }
        }
    }

    alm_update();            /* 报警判定 + 状态文本 14/16 */
}

/* ============================================================
 * 6. 趋势：历史曲线
 *
 * 屏 趋势.tft 里两个 historygraph 的 sample_data_foramt 必须是 2（UINT16），
 *   与下面的 HMI_HistorySampleU16() 对齐：若配成 0（UINT8），屏按 1 字节解析，
 *   只吃掉高字节 —— SpO2=97 即 0x0061，屏只取 0x00，曲线恒为 0 成一条直线。
 * 采样节奏 2s，屏 sample_cycle 也要同步改成 2。
 * 三个时段按钮为 1 分钟 / 5 分钟 / 30 分钟。
 * ============================================================ */
#define TREND_PERIOD_MS   2000u   /* 落点间隔，必须与 .tft 的 sample_cycle 一致 */
#define TREND_SAMPLE_SEC  2u      /* 一个点代表多少秒，用于时间刻度换算 */

static uint32_t t_trd = 0;

static uint16_t s_trd_count = 150u;   /* 当前窗口点数：30 / 150 / 900 */
static uint8_t  s_trend_sel = 2u;     /* 1=1分钟 2=5分钟 3=30分钟（按钮选中态） */
static uint32_t s_trd_min_shown = 0xFFFFFFFFu;  /* 上次写进屏的刻度分钟值，只有整分才重写 */

/* 屏在线探测重握手用：MCU 侧保存的趋势历史副本，便于屏断开/重启后"重发全部历史"。
 *   覆盖最大窗口（30 分钟 @2s = 900 点）；环形存储，最新的在写入位置之前。 */
#define SPO2_HIST_MAX  900u
static uint16_t s_hist_spo2[SPO2_HIST_MAX];
static uint16_t s_hist_pr[SPO2_HIST_MAX];
static uint16_t s_hist_cnt = 0;      /* 已存点数（<= MAX） */
static uint16_t s_hist_head = 0;     /* 下一个写入位置（环形） */
/* 重握手回放状态 */
static uint16_t s_replay_idx = 0;    /* 已回放点数 */
static uint8_t  s_replay_on  = 0;    /* 回放进行中标志 */
static uint32_t s_replay_tick = 0;   /* 上次回放分片时刻（时间门控） */

/* 趋势页的"非曲线"文案：左列最近值 10/11 + 曲线下方时间刻度 15..18。
   force=1 无条件整轮重发（切到趋势页 / 切换时段后屏会按默认属性重绘，必须补）。 */
static void trend_show_labels(uint8_t force)
{
    /* 刻度 i 距"现在"的偏移 = 窗口秒数 * num[i] / 3（0 号在最左=最早） */
    static const uint8_t num[4] = { 3u, 2u, 1u, 0u };
    uint32_t w = (uint32_t)s_trd_count * TREND_SAMPLE_SEC;   /* 窗口秒数 */
    uint32_t now_s = s_sec_of_day;
    uint8_t  i;
    char     b[8];

    /* (1) 最近值：暂停时保留暂停前最后一次有效值；数据无效时显示 -- */
    if (g_paused) {
        if (s_frozen_spo2 >= 0) {
            snprintf(b, sizeof(b), "%d", (int)s_frozen_spo2);
            HMI_SetText(SCR_TREND, TRD_TXT_LAST_SPO2, b);
            snprintf(b, sizeof(b), "%d", (int)s_frozen_pr);
            HMI_SetText(SCR_TREND, TRD_TXT_LAST_PR,   b);
        } else {
            strcpy(b, "--");
            HMI_SetText(SCR_TREND, TRD_TXT_LAST_SPO2, b);
            HMI_SetText(SCR_TREND, TRD_TXT_LAST_PR,   b);
        }
    } else if (!g_meas.result_valid || spo2_is_stale()) {
        strcpy(b, "--");
        HMI_SetText(SCR_TREND, TRD_TXT_LAST_SPO2, b);
        HMI_SetText(SCR_TREND, TRD_TXT_LAST_PR,   b);
    } else {
        snprintf(b, sizeof(b), "%d", (int)g_meas.spo2);
        HMI_SetText(SCR_TREND, TRD_TXT_LAST_SPO2, b);
        snprintf(b, sizeof(b), "%d", (int)g_meas.pulse_rate);
        HMI_SetText(SCR_TREND, TRD_TXT_LAST_PR,   b);
    }

    /* (2) 时间刻度：窗口内的时间点会随时间平移，但只有"分钟"变了才值得重写，
           否则每 2 秒白刷 4 帧。force=1 时（刚切页/刚换时段）强制重写。 */
    if (!force && s_trd_min_shown == (now_s / 60u)) return;
    s_trd_min_shown = now_s / 60u;

    for (i = 0u; i < 4u; i++) {
        uint32_t off = w * (uint32_t)num[i] / 3u;
        uint32_t t   = (now_s + 86400u - off) % 86400u;      /* 跨零点安全 */
        snprintf(b, sizeof(b), "%02u:%02u",
                 (unsigned)(t / 3600u), (unsigned)((t / 60u) % 60u));
        HMI_SetText(SCR_TREND, (uint16_t)(TRD_TXT_TIME_0 + i), b);
    }
}

void task_hmi_trend(void)
{
    uint16_t v[2];

    if ((int32_t)(millis() - t_trd) < 0) return;
    t_trd = millis() + TREND_PERIOD_MS;

    trend_show_labels(0u);             /* 最近值 / 时间刻度 */

    if (g_paused) return;              /* 暂停时不记录 */
    if (!g_meas.result_valid || spo2_is_stale()) return;  /* 无效/陈旧时不留洞 */

    v[0] = (uint16_t)g_meas.spo2;
    v[1] = (uint16_t)g_meas.pulse_rate;
    HMI_HistorySampleU16(SCR_TREND, TRD_GRAPH_SPO2, &v[0], 1);
    HMI_HistorySampleU16(SCR_TREND, TRD_GRAPH_PR,   &v[1], 1);

    /* 存本地历史副本（供屏在线探测重握手"重发全部历史"） */
    s_hist_spo2[s_hist_head] = v[0];
    s_hist_pr[s_hist_head]   = v[1];
    s_hist_head = (uint16_t)((s_hist_head + 1u) % SPO2_HIST_MAX);
    if (s_hist_cnt < SPO2_HIST_MAX) s_hist_cnt++;
}

/* ============================================================
 * 7. 屏按键 / 滑块 / 文本 回调（由 dc_hmi.c 的 HMI_Poll() 调进来）
 *
 * 按钮配置约定（依据官方例程与 VisualTFT 内 button_type 枚举）：
 *   button_type  0=切换画面（屏按 switch 属性自己跳页）/ 1=开关描述（只上报事件、不跳页）
 *   button_style 0=瞬变（点动按钮）/ 1=开关（切换型按钮）
 *   notify_disable 0=上传按键事件
 *   本工程 tftprj 里 DeviceControlNotify="3" = 按下和弹起都上报，
 *   所以一次点击会收到两帧（state=1 按下、state=0 弹起）——
 *   凡是翻转型动作都必须只在 state==1 时执行，否则一次点击会翻转两次（净变化为 0）。
 *   跳页分工：btn_confirm_pause / btn_confirm_clear 是 button_type=1 + switch=""，
 *   跳页由 MCU 收到通知后自己发 EE B1 00；取消/返回按钮仍是 button_type=0 原生跳页，
 *   作为"卡在弹窗页"的兜底出口。
 * ============================================================ */

/* 安排若干轮"MCU 负责内容"的补发（切页/跳页后屏会按控件默认属性重绘，必须补） */
static void request_resync(uint8_t rounds, uint16_t delay_ms)
{
    s_resync_round = rounds;
    s_resync_at    = millis() + delay_ms;
}

/* 切换趋势窗口：count = 一屏显示的采样点数（EE B1 63）。
 *   2 秒/点 -> 30 点=1 分钟、150 点=5 分钟、900 点=30 分钟（屏缓存 1024 点）。
 *   三个时段按钮是"开关型"（button_style=1），选中态由 MCU 用 EE B1 10 <u8> 回写。 */
static void set_trend_range(uint16_t count, const char *tip)
{
    s_trd_count = count;
    HMI_HistoryTimeLength(SCR_TREND, TRD_GRAPH_SPO2, count);
    HMI_HistoryTimeLength(SCR_TREND, TRD_GRAPH_PR,   count);
    HMI_SetText(SCR_TREND, TRD_TXT_PERIOD, tip);

    if      (count == 30u)  s_trend_sel = 1u;
    else if (count == 150u) s_trend_sel = 2u;
    else if (count == 900u) s_trend_sel = 3u;

    HMI_SetButton(SCR_TREND, TRD_BTN_1MIN,  (uint8_t)(s_trend_sel == 1u));
    HMI_SetButton(SCR_TREND, TRD_BTN_5MIN,  (uint8_t)(s_trend_sel == 2u));
    HMI_SetButton(SCR_TREND, TRD_BTN_30MIN, (uint8_t)(s_trend_sel == 3u));

    trend_show_labels(1u);   /* 窗口变了，时间刻度必须立刻按新窗口重算 */
}

void NotifyButton(uint16_t screen_id, uint16_t ctrl_id, uint8_t state)
{
    /* 必须先判边沿：DeviceControlNotify=3 -> 一次点击上报"按下"与"弹起"两帧。
       若把翻转型动作放在边沿判断之前，声音开关这类按钮会被翻转两次（等于没反应）。 */
    if (!state) return;                   /* 只处理按下沿 */

    /* 设置页"声音提示"：切换型按钮（button_style=1），按一次翻转一次 */
    if (screen_id == SCR_SETUP && ctrl_id == SET_BTN_SOUND) {
        s_sound_on = (uint8_t)(!s_sound_on);
        hmi_show_setup();                 /* 同步"声音提示已开启/已关闭" */
        return;
    }

    if (screen_id == SCR_MAIN && ctrl_id == MAIN_BTN_RESUME) {
        /* "继续监护"：button_type=1(只上报) + switch=""，不需跳页（本来就在主界面） */
        g_paused = 0;
        hmi_show_pause_ui(1u);
        return;
    }

    if (screen_id == SCR_PAUSE && ctrl_id == PAU_BTN_CONFIRM) {
        /* 弹窗"确认"：在暂停与恢复之间翻转。
           该按钮已改成 button_type=1 + switch=""，屏不会自己跳页，
           所以这里必须由 MCU 跳回主界面；先跳页再补发文案（屏重绘会清掉 MCU 写的内容）。 */
        /* 进入暂停的瞬间冻结此刻的有效读数：原冻结值由 400ms 刷新任务从 g_meas.pulse_rate
           取，可能正好采到峰检的瞬间假峰（如 226 这类生理不可能的 PR）而被冻结。
           这里在翻转 g_paused 之前直接快照当前读数，仅当数据有效才覆盖（否则保留上次
           有效值），保证暂停屏显示的正是"点暂停那一刻"的真实数据。 */
        if (g_paused == 0u && g_meas.result_valid) {
            s_frozen_spo2 = (int16_t)g_meas.spo2;
            s_frozen_pr   = (int16_t)g_meas.pulse_rate;
            s_frozen_sqi  = g_meas.signal_quality;
        }
        g_paused = (uint8_t)(!g_paused);
        HMI_SetScreen(SCR_MAIN);
        request_resync(4u, 120u);
        return;
    }

    if (screen_id == SCR_CLEAR && ctrl_id == CLR_BTN_CONFIRM) {
        alm_clear();                      /* 清空记录并刷新报警记录页 */
        HMI_SetScreen(SCR_ALARM);
        s_resync_alm = 1u;                /* 跳页后把 4 行文本再补几轮 */
        request_resync(4u, 120u);
        return;
    }

    if (screen_id == SCR_TREND) {
        if (ctrl_id == TRD_BTN_1MIN)       set_trend_range(30u,  S_TRD_1MIN);
        else if (ctrl_id == TRD_BTN_5MIN)  set_trend_range(150u, S_TRD_5MIN);
        else if (ctrl_id == TRD_BTN_30MIN) set_trend_range(900u, S_TRD_30MIN);
    }
}

/* 画面切换回调。屏每次跳转都会按控件默认属性重绘，这里安排"MCU 负责内容"的整体补发。 */
void NotifyScreen(uint16_t screen_id)
{
    screen_watch_touch(SCREEN_ID_SPO2);   /* 屏在线探测：B1 01 回复到达，证明血氧屏在线 */
    if (screen_id == SCR_MAIN || screen_id == SCR_PAUSE || screen_id == SCR_SETUP ||
        screen_id == SCR_TREND || screen_id == SCR_ALARM || screen_id == SCR_PROBE) {
        request_resync(4u, 120u);
    }
    if (screen_id == SCR_ALARM) s_resync_alm = 1u;   /* 报警记录页顺带补 4 行文本 */
    if (screen_id == SCR_SETUP) hmi_show_setup();
}

/* 设置页滑块：面板亮度。
   这里只写亮度百分比文本，不调用 hmi_show_setup()：
     hmi_show_setup() 会把滑块/进度条的值写回屏，若屏对"程序设值"也回通知，
     就会形成 滑块通知 -> 设滑块 -> 再通知 的死循环。文本控件无此风险。 */
void NotifySlider(uint16_t screen_id, uint16_t ctrl_id, uint32_t value)
{
    char b[16];

    if (screen_id != SCR_SETUP || ctrl_id != SET_SLIDER_BRIGHT) return;
    if (value < 10u)  value = 10u;
    if (value > 100u) value = 100u;

    s_bright_pct = (uint8_t)value;

    snprintf(b, sizeof(b), "%d%%", (int)s_bright_pct);
    HMI_SetText(SCR_SETUP, SET_TXT_BRIGHT, b);
    HMI_SetProgress(SCR_SETUP, SET_BAR_BRIGHT, (uint32_t)s_bright_pct);

    /* EE 60 的参数是"0=最亮 .. 255=最暗"，所以要做反相映射 */
    HMI_SetBackLight((uint8_t)((100u - value) * 255u / 100u));
}

/* 设置页文本输入：SpO2 下限 / 脉率上限 -> 更新 MCU 的报警阈值。
   越界值直接丢弃（SpO2 限 70~99、脉率限 60~200）；更新后立即重写主界面限值文案。 */
void NotifyText(uint16_t screen_id, uint16_t ctrl_id, const uint8_t *str, uint16_t len)
{
    char b[12];
    uint16_t n, i;
    int v;

    if (screen_id != SCR_SETUP) return;
    n = (len < (uint16_t)(sizeof(b) - 1)) ? len : (uint16_t)(sizeof(b) - 1);
    for (i = 0; i < n; i++) b[i] = (char)str[i];
    b[n] = '\0';

    v = atoi(b);
    if (ctrl_id == SET_TXT_SPO2_LOW) {
        if (v >= 70 && v <= 99)  { s_spo2_low = (int16_t)v; hmi_show_limits(1u); }
    } else if (ctrl_id == SET_TXT_PR_HIGH) {
        if (v >= 60 && v <= 200) { s_pr_high  = (int16_t)v; hmi_show_limits(1u); }
    }
}

/* ============================================================
 * 8. 应用层初始化：开机一次性动作
 * ============================================================ */
void app_hmi_init(void)
{
    uint16_t pg;

    /* 清掉旧的报警记录显示 + 探头状态条初始值 */
    alm_push_rows();
    s_probe_shown = 0xFF;      /* 强制下一次刷新把 91 写一遍 */

    /* 初始背光 80%（与设置页默认值一致，EE 60 参数反相） */
    HMI_SetBackLight((uint8_t)((100u - 80u) * 255u / 100u));

    /* 趋势：先确保两条曲线处于"允许采样"状态（屏属性 sample_enable=1 已是允许，
       这里再显式发一次 EE B1 61，避免屏固件版本差异导致不采样），
       再把初始窗口设为 5 分钟（150 点 x 2s，与按钮默认选中态一致）。 */
    HMI_HistoryEnable(SCR_TREND, TRD_GRAPH_SPO2, 1u);
    HMI_HistoryEnable(SCR_TREND, TRD_GRAPH_PR,   1u);
    set_trend_range(150u, S_TRD_5MIN);

    /* 报警记录页与探头页都过一遍，保证各页状态条一致 */
    for (pg = 0; pg <= (uint16_t)SCR_CLEAR; pg++) {
        HMI_SetText(pg, MAIN_TXT_PROBE, g_meas.probe_state ? S_PROBE_OK : S_PROBE_NO);
    }
    { uint8_t st = g_meas.probe_state ? 1u : 0u; s_probe_shown = st; }

    /* 设置屏 RTC：马上同步一次（软 RTC 从 RTC_INIT_* 开始走）。
     * 这里必须把"软 RTC 基准"落到两块屏共用的 g_rtc_epoch_tick 上：
     *   app_hmi_init() 在 app_init()（血压侧）之前调用，若两边各记各的基准，
     *   血压侧 0 秒会比血氧侧晚 Δ（本函数要刷几十帧屏指令，Δ 可达秒级），
     *   表现为"血压屏时间稳定比血氧屏慢 Δ 秒"。共用同一个 tick 后，
     *   两边 floor((tick-base)/1000) 完全一致，Δ 恒为 0。 */
    if (g_rtc_epoch_tick == 0u) g_rtc_epoch_tick = millis();
    s_rtc_last_tick = g_rtc_epoch_tick;
    s_rtc_last_sync = s_rtc_last_tick;   /* 下一次周期校准从此刻算起 */
    rtc_sync_to_screen();                /* 开机立即把时间写给屏 */

    /* ---- 下面这些过去由屏端 Lua 写，现在必须由 MCU 在开机时铺一遍 ---- */
    hmi_show_pause_ui(1u);    /* ① 藏 btn_resume、亮 btn_pause  ② 12/20 文案 ③ 时长 */
    hmi_show_limits(1u);      /* 主界面 13/15 报警限值文案 */
    hmi_show_setup();         /* 设置页 12 亮度百分比 / 13 声音提示状态 / 滑块回写 */
    hmi_show_duration(1u);    /* 主界面 21 监护时间 00:00:00 */
    s_resync_round = 0u;      /* 开机时不需要补发轮次 */
    s_resync_alm   = 0u;
    s_force_probe  = 0u;
}

/* ============================================================
 * 屏在线探测重握手（screen_watch 的 resync 回调）—— 血氧屏
 * ------------------------------------------------------------
 * 血氧屏掉线/重启后，趋势曲线（屏内 historygraph）内容丢失。
 * 这里把 MCU 侧保存的历史副本"重发全部历史"回灌到趋势曲线；同时重发趋势页文案
 * （最近值/时间刻度，由 trend_show_labels 负责）。主界面数值由 task_hmi_value()
 * 每 400ms 持续刷新，无需在此重复。
 *
 * 分片回放（每 50ms 最多 6 点），避免一次性发几百帧阻塞主循环、挤占 PPG 波形带宽。
 * 返回 1=完成，0=进行中。
 * ============================================================ */
uint8_t spo2_resync(void)
{
    uint32_t now = millis();
    uint16_t start, sent;

    if (!s_replay_on) {
        /* 启动回放：先重发趋势页文案（最近值/时间刻度），再从头灌历史 */
        trend_show_labels(1u);
        s_replay_idx = 0;
        s_replay_on  = 1;
        s_replay_tick = now;
        return 0u;   /* 本 tick 仅做初始化 */
    }

    /* 时间门控：每 50ms 最多回放若干点，避免刷屏洪泛 */
    if ((int32_t)(now - s_replay_tick) < 50) return 0u;
    s_replay_tick = now;

    start = (uint16_t)((s_hist_head + SPO2_HIST_MAX - s_hist_cnt) % SPO2_HIST_MAX);
    sent = 0;
    while (s_replay_idx < s_hist_cnt && sent < 6u) {
        uint16_t v[2];
        uint16_t src = (uint16_t)((start + s_replay_idx) % SPO2_HIST_MAX);
        v[0] = s_hist_spo2[src];
        v[1] = s_hist_pr[src];
        HMI_HistorySampleU16(SCR_TREND, TRD_GRAPH_SPO2, &v[0], 1);
        HMI_HistorySampleU16(SCR_TREND, TRD_GRAPH_PR,   &v[1], 1);
        s_replay_idx++;
        sent++;
    }

    if (s_replay_idx >= s_hist_cnt) {
        s_replay_on = 0;
        return 1u;   /* 全部历史重发完成 */
    }
    return 0u;       /* 还有剩余，下个 tick 继续 */
}
