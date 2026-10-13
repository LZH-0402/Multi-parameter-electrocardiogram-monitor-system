/*!
*  \file app_spo2.c
*  \brief 血氧监护应用层 —— 对应 VisualTFT 工程 SPO2_Hospital 的 7 个画面
*
*  画面与功能一览（ID 表见 hmi_ids.h）：
*    0 主界面          SpO2/脉率大字、PPG 实时波形、质量、监护时长、报警状态
*    1 趋势            2 条历史曲线 + 30分钟/2小时/8小时 时段切换
*    2 报警记录        4 行 × (时间/事件/级别/状态)，行按钮 -> 详情，清空按钮
*    3 设置            SpO2 下限、脉率上限、声音开关、背光滑块
*    4 暂停监护        确认后切换 监护/暂停
*    5 报警与探头状态   当前状态 + 处理建议 + 质量条 + 最近有效值
*    6 清空报警记录     确认后擦除记录
*
*  数据来源：meas.c（默认内置演示信号，可直接无硬件验证全部功能）
*  协议下发：hmi_driver.c 的 SetXxx() / BatchXxx() —— 本文件不出现协议字节
*
*  节拍（由 main 主循环不限速调用 App_Tick，内部按 10ms 分频）：
*    10ms   采样一拍 + 波形一点（攒够 WAVE_CHUNK 点发一帧）
*    100ms  报警判定（3s 去抖 + 滞回解除）
*    400ms  当前画面的数值刷新
*    1s     监护时长 +1、向屏同步一次 RTC
*    60s    向历史曲线追加一点
*/
#include "app_spo2.h"
#include "hmi_driver.h"
#include "hmi_ids.h"
#include "meas.h"
#include "cmd_process.h"
#include <stdio.h>
#include <string.h>

extern volatile uint32 timer_tick_count;      /* ulitity.c/SysTick 维护的 10ms 节拍 */

/* ==================== 1. 可调参数 ==================== */
#define APP_REFRESH_MS        400       /* 数值刷新周期                     */
#define ALARM_EVAL_MS         100       /* 报警判定周期                     */
#define RTC_SYNC_MS           1000      /* 向屏要一次 RTC 的周期            */
#define WAVE_CHUNK            10        /* 每帧送出的波形点数               */
#define WAVE_WIN              300       /* 归一化滑动窗口 = graph sample_count */
#define ALARM_HOLD_MS         3000      /* 越限持续多久才记一条             */
#define SPO2_REL_HYST         2         /* SpO2 回升到 下限+2 才解除        */
#define PR_REL_HYST           5         /* 脉率回落到 上限-5 才解除         */
#define TREND_SAMPLE_SEC      60        /* 历史曲线采样周期，须与 VisualTFT 中
                                           historygraph 的"采样周期"一致；
                                           现场想快速看到曲线就把它和屏幕一起改成 1 */
#define DEF_SPO2_LOW          90        /* SpO2 下限默认值                  */
#define DEF_PR_HIGH           120       /* 脉率上限默认值                   */
#define DEF_BRIGHT_PCT        80        /* 背光默认 %                       */
#define BED_TEXT              "床位 01"

#define ALM_ROWS              4         /* 报警记录行数（与画面控件数一致） */
#define ALM_NONE              0xFF      /* "没有选中行"                     */

static const uint16 s_range_min[3] = { 30, 120, 480 };      /* 分钟 */
static const char  *s_range_txt[3] = {
    "记录时段 30 分钟", "记录时段 2 小时", "记录时段 8 小时"
};

/* ==================== 2. 状态 ==================== */
static uint16 s_screen;                 /* 当前画面 ID                */
static uint8  s_running;                /* 1 监护中 / 0 已暂停        */
static uint8  s_sound_on;               /* 1 声音提示开               */
static uint8  s_spo2_low;               /* SpO2 下限                  */
static uint8  s_pr_high;                /* 脉率上限                   */
static uint8  s_bright;                 /* 背光 %                     */
static uint8  s_trend_sel;              /* 趋势时段 0/1/2             */
static uint8  s_probe_from_row;         /* 详情页来源行，ALM_NONE=实时 */
static uint32 s_mon_sec;                /* 监护时长(秒)               */
static uint32 s_last_tick;              /* 上一次执行的节拍           */
static uint8  s_rx_seen;                /* 是否已收到过屏的任何通知   */
static uint8  s_repush;                 /* 上电后补发初值的次数       */

/* 屏幕 RTC 缓存（十进制） */
static uint8  s_rtc_valid;
static uint8  s_rtc[6];                 /* 年 月 日 时 分 秒 */
static uint8  s_valid_t[3];             /* 最近一次有效测量的 时:分:秒 */
static uint8  s_valid_t_ok;

/* 报警记录：s_rec[0] 永远是最新一条 */
typedef struct
{
    char t[10];                         /* HH:MM:SS */
    char ev[16];                        /* 事件     */
    char lv[8];                         /* 级别     */
    char st[8];                         /* 状态     */
} alm_rec_t;

static alm_rec_t s_rec[ALM_ROWS];
static uint8     s_rec_used;            /* 已占用行数 0~4 */
static uint16    s_rec_total;           /* 累计条数       */

/* 报警引擎 */
static uint8  s_a_spo2;                 /* 血氧报警激活中 */
static uint8  s_a_pr;                   /* 脉率报警激活中 */
static uint16 s_spo2_below;             /* 连续越限的判定次数 */
static uint16 s_pr_above;

/* 实时波形 */
static uint8  s_wbuf[WAVE_CHUNK];
static uint8  s_wcnt;
static int32  s_win[WAVE_WIN];
static uint16 s_whead;
static uint16 s_wfill;

/* ==================== 3. 小工具 ==================== */
static void cpy(char *d, unsigned int n, const char *s)
{
    unsigned int i = 0;
    if (n == 0) return;
    while ((i + 1 < n) && (s[i] != '\0')) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static uint8 bcd2i(uint8 b)
{
    return (uint8)(((b >> 4) & 0x0F) * 10 + (b & 0x0F));
}

/* "HH:MM:SS"，没有同步到屏时间时用 --:--:-- */
static void fmt_clock(char *out, unsigned int n)
{
    if (!s_rtc_valid) { cpy(out, n, "--:--:--"); return; }
    sprintf(out, "%02d:%02d:%02d", (int)s_rtc[3], (int)s_rtc[4], (int)s_rtc[5]);
}

/* 一天中的第几分钟 -> "HH:MM"（out 至少 6 字节） */
static void fmt_tod(int tod, char *out)
{
    while (tod < 0) tod += 1440;
    tod %= 1440;
    sprintf(out, "%02d:%02d", tod / 60, tod % 60);
}

/* 背光度：大彩约定 0 最亮、255 最暗（见官方 DCDEMO7 例程注释）。
*  若实测方向相反，把这里改成 (uint8)(pct*255/100) 即可。 */
static uint8 backlight_level(uint8 pct)
{
    if (pct > 100) pct = 100;
    return (uint8)(255 - (uint16)pct * 2);
}

static const char *probe_text(void)
{
    if (g_meas.probe == PROBE_OFF)  return "探头未接入";
    if (g_meas.probe == PROBE_WEAK) return "探头信号弱";
    return "探头已接入";
}

/* ==================== 4. 报警记录 ==================== */
static void rec_insert(const char *ev, const char *lv)
{
    uint8 i;

    if (s_rec_used < ALM_ROWS) s_rec_used++;

    for (i = ALM_ROWS - 1; i > 0; i--) s_rec[i] = s_rec[i - 1];

    fmt_clock(s_rec[0].t, sizeof(s_rec[0].t));
    cpy(s_rec[0].ev, sizeof(s_rec[0].ev), ev);
    cpy(s_rec[0].lv, sizeof(s_rec[0].lv), lv);
    cpy(s_rec[0].st, sizeof(s_rec[0].st), "未处理");
    s_rec_total++;
}

/* 把某个事件的"未处理"行改成"已解除" */
static void rec_release(const char *ev)
{
    uint8 i;
    for (i = 0; i < ALM_ROWS; i++)
    {
        if ((strcmp(s_rec[i].ev, ev) == 0) && (strcmp(s_rec[i].st, "未处理") == 0))
        {
            cpy(s_rec[i].st, sizeof(s_rec[i].st), "已解除");
            return;
        }
    }
}

static void rec_clear(void)
{
    memset(s_rec, 0, sizeof(s_rec));
    s_rec_used  = 0;
    s_rec_total = 0;
    s_a_spo2    = 0;
    s_a_pr      = 0;
}

/* ==================== 5. 画面刷新 ==================== */
/* 每页都有的状态条（床位 / 探头） */
static void refresh_common(void)
{
    SetTextValue(s_screen, COM_TXT_BED, (uchar *)BED_TEXT);
    SetTextValue(s_screen, COM_TXT_PROBE_HEADER, (uchar *)probe_text());
}

/* ---- 主界面 ---- */
static void refresh_main(void)
{
    char spo2[8], pr[8], q[8], dur[28];
    char valid[20], sst[20], pst[20], cap[20];
    char slim[28], plim[28];

    if (s_running && g_meas.valid && (g_meas.spo2 >= 0))
        sprintf(spo2, "%d", (int)g_meas.spo2);
    else
        cpy(spo2, sizeof(spo2), "--");

    if (s_running && g_meas.valid && (g_meas.pulse_rate >= 0))
        sprintf(pr, "%d", (int)g_meas.pulse_rate);
    else
        cpy(pr, sizeof(pr), "--");

    sprintf(q, "%d", (int)g_meas.quality);

    sprintf(dur, "监护时间 %02d:%02d:%02d",
            (int)(s_mon_sec / 3600u), (int)(s_mon_sec % 3600u / 60u), (int)(s_mon_sec % 60u));
    sprintf(slim, "报警限值 %d-100 %%", (int)s_spo2_low);
    sprintf(plim, "报警高限 %d bpm", (int)s_pr_high);

    /* 有效性 / 状态文本 */
    if (!s_running)
    {
        /* txt_valid 只有 66px 宽(4 个字)，放不下"监护已暂停"，用短文案 */
        cpy(valid, sizeof(valid), "已暂停");
        cpy(sst,   sizeof(sst),   "--");
        cpy(pst,   sizeof(pst),   "--");
        cpy(cap,   sizeof(cap),   "监护已暂停");
    }
    else if (g_meas.probe == PROBE_OFF)
    {
        cpy(valid, sizeof(valid), "信号无效");
        cpy(sst,   sizeof(sst),   "--");
        cpy(pst,   sizeof(pst),   "--");
        cpy(cap,   sizeof(cap),   "请接入血氧探头");
    }
    else
    {
        cpy(valid, sizeof(valid), g_meas.valid ? "测量有效" : "信号无效");
        cpy(sst,   sizeof(sst),   s_a_spo2 ? "血氧偏低" : (g_meas.probe == PROBE_WEAK ? "信号偏弱" : "稳定"));
        cpy(pst,   sizeof(pst),   s_a_pr   ? "脉率偏快" : (g_meas.probe == PROBE_WEAK ? "信号偏弱" : "脉搏规则"));
        cpy(cap,   sizeof(cap),   (g_meas.probe == PROBE_WEAK) ? "信号偏弱，等待稳定" : "脉搏波采集中");
    }

    SetScreenUpdateEnable(0);                        /* 关刷新，避免逐条闪烁 */

    BatchBegin(SCR_MAIN);
    BatchSetText(MAIN_TXT_SPO2,          (uchar *)spo2);
    BatchSetText(MAIN_TXT_PR,            (uchar *)pr);
    BatchSetText(MAIN_TXT_VALID,         (uchar *)valid);
    BatchSetText(MAIN_TXT_SPO2_STATE,    (uchar *)sst);
    BatchSetText(MAIN_TXT_PR_STATE,      (uchar *)pst);
    BatchSetText(MAIN_TXT_QUALITY,       (uchar *)q);
    BatchSetText(MAIN_TXT_CAPTURE_STATE, (uchar *)cap);
    BatchSetText(MAIN_TXT_DURATION,      (uchar *)dur);
    BatchSetText(MAIN_TXT_SPO2_LIMITS,   (uchar *)slim);
    BatchSetText(MAIN_TXT_PR_LIMIT,      (uchar *)plim);
    BatchEnd();

    SetScreenUpdateEnable(1);                        /* 一次性刷新出来 */

    /* 质量图标(控件 18)是单个"图片"控件，本固件的指令集里没有"改图片控件显示哪张图"
       的指令（只有 0x10 通用写值/按钮/进度条），所以无法按 0~5 级切图。
       折中做法：信号差(质量 < 60)时把它隐藏，避免一直显示满格图标误导；
       若要真正分级，需要在 VisualTFT 里放 6 个图片控件再用 SetControlVisiable 互斥显示。 */
    {
        static uint8 s_last_icon_vis = 0xFF;
        uint8 vis = (uint8)((g_meas.valid && (g_meas.quality >= 60)) ? 1 : 0);
        if (vis != s_last_icon_vis)
        {
            s_last_icon_vis = vis;
            SetControlVisiable(SCR_MAIN, MAIN_IMG_QUALITY_ICON, vis);
        }
    }
}

/* ---- 趋势 ---- */
static void refresh_trend_labels(void);         /* 定义见下方 set_trend_range 之后 */

/* 趋势页的"最近值"：背景图右侧已经画好了 " % " 与 " bpm "，
   这两个控件只有 62px 宽，只能放数字本身 */
static void refresh_trend_live(void)
{
    char spo2[8], pr[8];

    if (g_meas.valid && (g_meas.spo2 >= 0))
        sprintf(spo2, "%d", (int)g_meas.spo2);
    else
        cpy(spo2, sizeof(spo2), "--");

    if (g_meas.valid && (g_meas.pulse_rate >= 0))
        sprintf(pr, "%d", (int)g_meas.pulse_rate);
    else
        cpy(pr, sizeof(pr), "--");

    SetTextValue(SCR_TREND, TRD_TXT_LAST_SPO2, (uchar *)spo2);
    SetTextValue(SCR_TREND, TRD_TXT_LAST_PR,   (uchar *)pr);
}

/* 时段切换：历史曲线一屏点数 + 按钮互斥选中 + 时段文本 + 时间刻度 */
static void set_trend_range(uint8 sel)
{
    uint16 pts;

    if (sel > 2) sel = 2;
    s_trend_sel = sel;

    /* 一屏点数 = 时长(秒) / 采样周期 */
    pts = (uint16)((uint32)s_range_min[sel] * 60u / TREND_SAMPLE_SEC);
    HistoryGraph_SetTimeLength(SCR_TREND, TRD_GRAPH_SPO2, pts);
    HistoryGraph_SetTimeLength(SCR_TREND, TRD_GRAPH_PR,   pts);

    /* 三个时段按钮是自锁型(button_type=1)，选中态必须由 MCU 驱动 */
    SetButtonValue(SCR_TREND, TRD_BTN_RANGE_30, (uchar)(sel == 0));
    SetButtonValue(SCR_TREND, TRD_BTN_RANGE_2H, (uchar)(sel == 1));
    SetButtonValue(SCR_TREND, TRD_BTN_RANGE_8H, (uchar)(sel == 2));

    SetTextValue(SCR_TREND, TRD_TXT_PERIOD, (uchar *)s_range_txt[sel]);

    refresh_trend_labels();
}

/* 只刷新 4 个时间刻度（每追加一个趋势点后调用，不能重设曲线长度，
   否则会把已画出来的曲线视图重置掉） */
static void refresh_trend_labels(void)
{
    char lbl[8];
    uint8 i;
    int tod, off;

    for (i = 0; i < 4; i++)
    {
        if (s_rtc_valid)
        {
            tod = (int)s_rtc[3] * 60 + (int)s_rtc[4];
            off = (int)s_range_min[s_trend_sel] - (int)s_range_min[s_trend_sel] * (int)i / 3;
            fmt_tod(tod - off, lbl);
            SetTextValue(SCR_TREND, (uint16)(TRD_TXT_TIME_0 + i), (uchar *)lbl);
        }
        else
        {
            SetTextValue(SCR_TREND, (uint16)(TRD_TXT_TIME_0 + i), (uchar *)"--:--");
        }
    }
}

/* ---- 报警记录 ---- */
static void refresh_alarm(void)
{
    char cnt[24];
    uint8 i;

    /* txt_event_count 只有 43px 宽（背景图上"事件记录 … | 按时间排序"之间），
       只够写数字本身，写不下"共 N 条记录" */
    sprintf(cnt, "%d", (int)s_rec_total);

    SetScreenUpdateEnable(0);
    BatchBegin(SCR_ALARM);
    BatchSetText(ALM_TXT_EVENT_COUNT, (uchar *)cnt);
    BatchSetText(ALM_TXT_EMPTY, (uchar *)(s_rec_used ? "" : "暂无报警记录"));

    for (i = 1; i <= ALM_ROWS; i++)
    {
        if (i <= s_rec_used)
        {
            BatchSetText(ALM_ROW_TIME(i),  (uchar *)s_rec[i - 1].t);
            BatchSetText(ALM_ROW_EVENT(i), (uchar *)s_rec[i - 1].ev);
            BatchSetText(ALM_ROW_LEVEL(i), (uchar *)s_rec[i - 1].lv);
            BatchSetText(ALM_ROW_STATE(i), (uchar *)s_rec[i - 1].st);
        }
        else
        {
            BatchSetText(ALM_ROW_TIME(i),  (uchar *)"");
            BatchSetText(ALM_ROW_EVENT(i), (uchar *)"");
            BatchSetText(ALM_ROW_LEVEL(i), (uchar *)"");
            BatchSetText(ALM_ROW_STATE(i), (uchar *)"");
        }
    }
    BatchEnd();
    SetScreenUpdateEnable(1);
}

/* ---- 设置 ---- */
static void refresh_setup(void)
{
    char b[8];

    sprintf(b, "%d", (int)s_spo2_low);
    SetTextValue(SCR_SETUP, SET_TXT_SPO2_LOW, (uchar *)b);

    sprintf(b, "%d", (int)s_pr_high);
    SetTextValue(SCR_SETUP, SET_TXT_PR_HIGH, (uchar *)b);

    SetProgressValue(SCR_SETUP, SET_BAR_BRIGHTNESS, s_bright);
    sprintf(b, "%d%%", (int)s_bright);
    SetTextValue(SCR_SETUP, SET_TXT_BRIGHTNESS, (uchar *)b);

    SetButtonValue(SCR_SETUP, SET_BTN_SOUND, s_sound_on);
    SetTextValue(SCR_SETUP, SET_TXT_SOUND_STATE,
                 (uchar *)(s_sound_on ? "声音提示已开启" : "声音提示已关闭"));
}

/* ---- 暂停监护 ---- */
static void refresh_pause(void)
{
    SetTextValue(SCR_PAUSE, PAU_TXT_CONTEXT,
                 (uchar *)(s_running ? "当前床位 01 血氧与脉率监护"
                                     : "监护已暂停，按确认恢复"));
}

/* ---- 报警与探头状态 ---- */
static void refresh_probe(void)
{
    char q[8], spo2[16], pr[16], upd[32], vt[32];
    char title[20], name[24], adv[72], foot[24];
    uint8 by_row;

    by_row = (uint8)((s_probe_from_row != ALM_NONE) && (s_probe_from_row < s_rec_used));

    if (by_row)
    {
        /* 从报警记录某一行点进来 -> 显示该条报警的详情 */
        cpy(title, sizeof(title), "报警详情");
        cpy(name,  sizeof(name),  s_rec[s_probe_from_row].ev);
        if (strcmp(s_rec[s_probe_from_row].ev, "血氧偏低") == 0)
            cpy(adv, sizeof(adv), "血氧低于报警下限，请检查患者状态并通知医生。");
        else if (strcmp(s_rec[s_probe_from_row].ev, "脉率偏快") == 0)
            cpy(adv, sizeof(adv), "脉率高于报警上限，请确认测量结果并持续观察。");
        else
            cpy(adv, sizeof(adv), "请结合现场情况处理。");
        cpy(foot, sizeof(foot), s_rec[s_probe_from_row].st);
    }
    else
    {
        cpy(title, sizeof(title), "当前状态");
        if (!s_running)
        {
            cpy(name, sizeof(name), "监护已暂停");
            cpy(adv,  sizeof(adv),  "监护暂停中，按确认键可恢复监护。");
            cpy(foot, sizeof(foot), "已暂停");
        }
        else if (g_meas.probe == PROBE_OFF)
        {
            cpy(name, sizeof(name), "探头未接入");
            cpy(adv,  sizeof(adv),  "请将血氧探头夹在食指上，并保持手指温暖静止。");
            cpy(foot, sizeof(foot), "等待探头接入");
        }
        else if (s_a_spo2)
        {
            cpy(name, sizeof(name), "血氧偏低");
            cpy(adv,  sizeof(adv),  "血氧低于报警下限，请检查患者状态并通知医生。");
            cpy(foot, sizeof(foot), "报警中，请及时处理");
        }
        else if (s_a_pr)
        {
            cpy(name, sizeof(name), "脉率偏快");
            cpy(adv,  sizeof(adv),  "脉率高于报警上限，请确认测量结果并持续观察。");
            cpy(foot, sizeof(foot), "报警中，请及时处理");
        }
        else if (g_meas.probe == PROBE_WEAK)
        {
            cpy(name, sizeof(name), "信号偏弱");
            cpy(adv,  sizeof(adv),  "请调整探头位置，并保持手指静止、末梢温暖。");
            cpy(foot, sizeof(foot), "等待稳定信号");
        }
        else
        {
            cpy(name, sizeof(name), "监护正常");
            cpy(adv,  sizeof(adv),  "信号稳定，继续监护。");
            cpy(foot, sizeof(foot), "信号稳定，正在监护");
        }
    }

    sprintf(q, "%d", (int)g_meas.quality);
    if (g_meas.valid && (g_meas.spo2 >= 0))      sprintf(spo2, "%d %%", (int)g_meas.spo2);
    else                                          cpy(spo2, sizeof(spo2), "-- %");
    if (g_meas.valid && (g_meas.pulse_rate >= 0)) sprintf(pr, "%d bpm", (int)g_meas.pulse_rate);
    else                                          cpy(pr, sizeof(pr), "-- bpm");

    sprintf(upd, "状态更新时间 %02d:%02d:%02d",
            (int)s_rtc[3], (int)s_rtc[4], (int)s_rtc[5]);
    if (!s_rtc_valid) cpy(upd, sizeof(upd), "状态更新时间 --:--:--");

    if (s_valid_t_ok) sprintf(vt, "最近有效时间 %02d:%02d:%02d",
                              (int)s_valid_t[0], (int)s_valid_t[1], (int)s_valid_t[2]);
    else              cpy(vt, sizeof(vt), "最近有效时间 --:--:--");

    SetScreenUpdateEnable(0);
    BatchBegin(SCR_PROBE);
    BatchSetText(PRB_TXT_TITLE,      (uchar *)title);
    BatchSetText(PRB_TXT_NAME,       (uchar *)name);
    BatchSetText(PRB_TXT_ADVICE,     (uchar *)adv);
    BatchSetText(PRB_TXT_PROBE,      (uchar *)probe_text());
    BatchSetText(PRB_TXT_UPDATE,     (uchar *)upd);
    BatchSetText(PRB_TXT_QUALITY,    (uchar *)q);
    BatchSetText(PRB_TXT_LAST_SPO2,  (uchar *)spo2);
    BatchSetText(PRB_TXT_LAST_PR,    (uchar *)pr);
    BatchSetText(PRB_TXT_VALID_TIME, (uchar *)vt);
    BatchSetText(PRB_TXT_FOOTER,     (uchar *)foot);
    BatchSetProgressValue(PRB_BAR_QUALITY, g_meas.quality);
    BatchEnd();
    SetScreenUpdateEnable(1);
}

/* ---- 清空报警记录 ---- */
static void refresh_clear(void)
{
    SetTextValue(SCR_CLEAR, CLR_TXT_CONTEXT, (uchar *)"当前床位 01 事件记录");
}

/* 只刷新当前画面上的"活数据" */
static void refresh_live(void)
{
    switch (s_screen)
    {
    case SCR_MAIN:  refresh_main();       break;
    case SCR_TREND: refresh_trend_live(); break;
    case SCR_PROBE: refresh_probe();      break;
    default:                              break;
    }
}

/* ==================== 6. 波形 / 报警 / 趋势 ==================== */
/* 一个 PPG 采样点 -> 滑动窗极值归一化到 0~255 -> 攒够一帧发出去 */
static void wave_step(void)
{
    int32  raw;
    int32  mn, mx, span, v;
    uint16 i;

    raw = Meas_PpgSample();

    s_win[s_whead] = raw;
    s_whead = (uint16)((s_whead + 1u) % WAVE_WIN);
    if (s_wfill < WAVE_WIN) s_wfill++;

    mn = mx = s_win[0];
    for (i = 1; i < s_wfill; i++)
    {
        if (s_win[i] < mn) mn = s_win[i];
        if (s_win[i] > mx) mx = s_win[i];
    }

    span = mx - mn;
    if (span < 1) span = 1;

    v = (int32)(((raw - mn) * 255) / span);
    if (v < 0)   v = 0;
    if (v > 255) v = 255;

    s_wbuf[s_wcnt++] = (uint8)v;

    if (s_wcnt >= WAVE_CHUNK)
    {
        GraphChannelDataAdd(SCR_MAIN, MAIN_GRAPH_PPG, 0, s_wbuf, WAVE_CHUNK);
        s_wcnt = 0;
    }
}

/* 每 100ms 一次：越限持续 3s 才记一条，滞回后自动解除 */
static void alarm_eval(void)
{
    const uint16 HOLD = (uint16)(ALARM_HOLD_MS / ALARM_EVAL_MS);
    int16 spo2, pr;

    if (!s_running || !g_meas.valid)
    {
        s_spo2_below = 0;
        s_pr_above   = 0;
        return;
    }

    spo2 = g_meas.spo2;
    pr   = g_meas.pulse_rate;

    /* ---------------- 血氧下限 ---------------- */
    if ((spo2 >= 0) && ((uint8)spo2 < s_spo2_low))
    {
        if (s_spo2_below < HOLD + 5) s_spo2_below++;
        if (!s_a_spo2 && (s_spo2_below >= HOLD))
        {
            s_a_spo2 = 1;
            rec_insert("血氧偏低", "高级");
            if (s_sound_on) SetBuzzer(200);
            refresh_alarm();
        }
    }
    else
    {
        s_spo2_below = 0;
        if (s_a_spo2 && (spo2 >= (int16)(s_spo2_low + SPO2_REL_HYST)))
        {
            s_a_spo2 = 0;
            rec_release("血氧偏低");
            refresh_alarm();
        }
    }

    /* ---------------- 脉率上限 ---------------- */
    if ((pr >= 0) && ((uint8)pr > s_pr_high))
    {
        if (s_pr_above < HOLD + 5) s_pr_above++;
        if (!s_a_pr && (s_pr_above >= HOLD))
        {
            s_a_pr = 1;
            rec_insert("脉率偏快", "中级");
            if (s_sound_on) SetBuzzer(200);
            refresh_alarm();
        }
    }
    else
    {
        s_pr_above = 0;
        if (s_a_pr && (pr <= (int16)(s_pr_high - PR_REL_HYST)))
        {
            s_a_pr = 0;
            rec_release("脉率偏快");
            refresh_alarm();
        }
    }
}

/* 每 TREND_SAMPLE_SEC 秒向两条历史曲线各追加一个点 */
static void trend_step(void)
{
    uint16 v;

    if (!s_running || !g_meas.valid) return;

    if (g_meas.spo2 >= 0)
    {
        v = (uint16)g_meas.spo2;
        HistoryGraph_SetValueInt16(SCR_TREND, TRD_GRAPH_SPO2, &v, 1);
    }
    if (g_meas.pulse_rate >= 0)
    {
        v = (uint16)g_meas.pulse_rate;
        HistoryGraph_SetValueInt16(SCR_TREND, TRD_GRAPH_PR, &v, 1);
    }

    if (s_screen == SCR_TREND) { refresh_trend_live(); refresh_trend_labels(); }
}

/* ==================== 7. 对外接口 ==================== */
void App_Init(void)
{
    s_screen          = SCR_MAIN;
    s_running         = 1;
    s_sound_on        = 1;
    s_spo2_low        = DEF_SPO2_LOW;
    s_pr_high         = DEF_PR_HIGH;
    s_bright          = DEF_BRIGHT_PCT;
    s_trend_sel       = 0;
    s_probe_from_row  = ALM_NONE;
    s_mon_sec         = 0;
    s_last_tick       = 0;
    s_rx_seen         = 0;
    s_repush          = 0;
    s_rtc_valid       = 0;
    s_valid_t_ok      = 0;
    s_whead           = 0;
    s_wfill           = 0;
    s_wcnt            = 0;
    s_a_spo2          = 0;
    s_a_pr            = 0;
    s_spo2_below      = 0;
    s_pr_above        = 0;

    memset(s_rtc, 0, sizeof(s_rtc));
    memset(s_win, 0, sizeof(s_win));
    memset(s_valid_t, 0, sizeof(s_valid_t));
    rec_clear();

    Meas_Init();

    SetScreen(SCR_MAIN);
    App_PushAll();
}

void App_PushAll(void)
{
    refresh_common();
    refresh_main();
    set_trend_range(s_trend_sel);
    refresh_trend_live();
    refresh_alarm();
    refresh_setup();
    refresh_probe();
    refresh_pause();
    refresh_clear();
}

uint16 App_CurrentScreen(void) { return s_screen; }
uint8  App_IsRunning(void)     { return s_running; }

void App_Tick(void)
{
    uint32 now = timer_tick_count;

    if (now == s_last_tick) return;          /* 每个 10ms 节拍只跑一遍 */
    s_last_tick = now;

    /* --- 10ms：采样 + 波形 --- */
    Meas_Tick();
    if (s_running) wave_step();
    else           s_wcnt = 0;

    /* --- 100ms：报警判定 --- */
    if ((now % (ALARM_EVAL_MS / 10u)) == 0) alarm_eval();

    /* --- 400ms：当前画面数值刷新 --- */
    if ((now % (APP_REFRESH_MS / 10u)) == 0) refresh_live();

    /* --- 1s：监护时长 + 屏 RTC 同步 --- */
    if ((now % 100u) == 0)
    {
        if (s_running) s_mon_sec++;

        if (s_running && g_meas.valid && s_rtc_valid)
        {
            s_valid_t[0] = s_rtc[3];
            s_valid_t[1] = s_rtc[4];
            s_valid_t[2] = s_rtc[5];
            s_valid_t_ok = 1;
        }
        ReadRTC();                            /* 屏回包 -> NotifyReadRTC() */

        /* 屏比 MCU 起得慢时，上电那几帧会丢掉。只要一直没收到屏的任何回包，
           就每秒补发一遍界面初值，最多 5 次（屏一旦应答就停）。 */
        if (!s_rx_seen && (s_repush < 5))
        {
            s_repush++;
            App_PushAll();
        }
    }

    /* --- TREND_SAMPLE_SEC：历史曲线追加一点 --- */
    if ((now % (uint32)(TREND_SAMPLE_SEC * 100u)) == 0) trend_step();
}

/* ==================== 8. 屏 -> MCU 的通知回调 ==================== */
/* 画面切换通知 */
void NotifyScreen(uint16 screen_id)
{
    if (screen_id >= SCR_COUNT) return;

    s_rx_seen = 1;
    s_screen  = screen_id;
    if (screen_id != SCR_PROBE) s_probe_from_row = ALM_NONE;   /* 进入详情页时保留来源行 */

    refresh_common();
    refresh_live();

    if (screen_id == SCR_TREND) set_trend_range(s_trend_sel);
    if (screen_id == SCR_ALARM) refresh_alarm();
    if (screen_id == SCR_SETUP) refresh_setup();
    if (screen_id == SCR_PAUSE) refresh_pause();
    if (screen_id == SCR_CLEAR) refresh_clear();
}

/* 按钮通知
*  画面里的按钮分两类，处理方式不同：
*   button_type=0 瞬变按钮（主界面 5 个导航键、各页"返回"、暂停/清空的
*                 取消与确认）：屏上报两次 —— 按下 state=1、松开 state=0，
*                 只处理 state=1。
*   button_type=1 自锁按钮（趋势页 3 个时段键、设置页声音开关）：屏上报的是
*                 翻转后的"新状态"，关闭时就是 0，因此不能过滤 state==0，
*                 否则会出现"只能开、不能关"的问题。
*/
void NotifyButton(uint16 screen_id, uint16 control_id, uint8 state)
{
    s_rx_seen = 1;

    /* ---- 自锁按钮：不看 state 是否等于 0 ---- */
    if (screen_id == SCR_TREND)
    {
        if      (control_id == TRD_BTN_RANGE_30) set_trend_range(0);
        else if (control_id == TRD_BTN_RANGE_2H) set_trend_range(1);
        else if (control_id == TRD_BTN_RANGE_8H) set_trend_range(2);
        return;                                  /* 该页其余按钮由屏自行跳转 */
    }
    if (screen_id == SCR_SETUP)
    {
        if (control_id == SET_BTN_SOUND)
        {
            s_sound_on = (state != 0);
            refresh_setup();
            refresh_main();
        }
        return;
    }

    /* ---- 瞬变按钮：只处理按下 ---- */
    if (state == 0) return;

    switch (screen_id)
    {
    case SCR_MAIN:
        /* 5 个导航按钮的跳转由屏自己完成，这里只做业务补充 */
        if (control_id == MAIN_BTN_PAUSE)      refresh_pause();
        else if (control_id == MAIN_BTN_PROBE) { s_probe_from_row = ALM_NONE; refresh_probe(); }
        break;

    case SCR_ALARM:
        if ((control_id >= ALM_BTN_ROW_1) && (control_id <= ALM_BTN_ROW_4))
        {
            s_probe_from_row = (uint8)(control_id - ALM_BTN_ROW_1);
            refresh_probe();
        }
        break;

    case SCR_PAUSE:
        if (control_id == PAU_BTN_CONFIRM)
        {
            /* 屏已自动跳回主界面，这里真正切换监护状态 */
            s_running = s_running ? 0 : 1;
            Meas_SetRun(s_running);
            s_spo2_below = 0;
            s_pr_above   = 0;
            if (!s_running) { s_a_spo2 = 0; s_a_pr = 0; }
            refresh_pause();
            refresh_main();
            refresh_probe();
        }
        break;

    case SCR_CLEAR:
        if (control_id == CLR_BTN_CONFIRM)
        {
            rec_clear();
            refresh_alarm();
            refresh_probe();
        }
        break;

    default:
        break;
    }
}

/* 文本输入：设置页的两个限值 */
void NotifyText(uint16 screen_id, uint16 control_id, uint8 *str)
{
    int32 value = 0;
    char  b[8];

    if (screen_id != SCR_SETUP) return;

    sscanf((char *)str, "%ld", &value);

    if (control_id == SET_TXT_SPO2_LOW)
    {
        if (value < 60)  value = 60;
        if (value > 99)  value = 99;
        s_spo2_low = (uint8)value;
        sprintf(b, "%d", (int)s_spo2_low);
        SetTextValue(SCR_SETUP, SET_TXT_SPO2_LOW, (uchar *)b);
        refresh_main();
    }
    else if (control_id == SET_TXT_PR_HIGH)
    {
        if (value < 60)  value = 60;
        if (value > 200) value = 200;
        s_pr_high = (uint8)value;
        sprintf(b, "%d", (int)s_pr_high);
        SetTextValue(SCR_SETUP, SET_TXT_PR_HIGH, (uchar *)b);
        refresh_main();
    }
}

/* 进度条：设置页亮度进度条被直接拖动时同步滑块与文本 */
void NotifyProgress(uint16 screen_id, uint16 control_id, uint32 value)
{
    if ((screen_id == SCR_SETUP) && (control_id == SET_BAR_BRIGHTNESS))
    {
        if (value < 10)  value = 10;
        if (value > 100) value = 100;
        s_bright = (uint8)value;
        SetSliderValue(SCR_SETUP, SET_SLIDER_BRIGHTNESS, value);
        NotifySlider(screen_id, SET_SLIDER_BRIGHTNESS, value);
    }
}

/* 滑块：设置页背光亮度 */
void NotifySlider(uint16 screen_id, uint16 control_id, uint32 value)
{
    char b[8];

    if ((screen_id != SCR_SETUP) || (control_id != SET_SLIDER_BRIGHTNESS)) return;

    if (value < 10)  value = 10;
    if (value > 100) value = 100;
    s_bright = (uint8)value;

    SetProgressValue(SCR_SETUP, SET_BAR_BRIGHTNESS, value);
    sprintf(b, "%d%%", (int)s_bright);
    SetTextValue(SCR_SETUP, SET_TXT_BRIGHTNESS, (uchar *)b);
    SetBackLight(backlight_level(s_bright));
}

/* RTC 回包：屏返回的是 BCD 码，转成十进制缓存 */
void NotifyReadRTC(uint8 year, uint8 month, uint8 week, uint8 day,
                   uint8 hour, uint8 minute, uint8 second)
{
    (void)week;

    s_rx_seen = 1;
    s_rtc[0] = bcd2i(year);
    s_rtc[1] = bcd2i(month);
    s_rtc[2] = bcd2i(day);
    s_rtc[3] = bcd2i(hour);
    s_rtc[4] = bcd2i(minute);
    s_rtc[5] = bcd2i(second);
    s_rtc_valid = 1;
}

/* ---- 本工程用不到的回调，保留空实现 ---- */
void NotifyTouchXY(uint8 press, uint16 x, uint16 y) { (void)press; (void)x; (void)y; }
void NotifyMeter(uint16 screen_id, uint16 control_id, uint32 value)
{ (void)screen_id; (void)control_id; (void)value; }
void NotifyMenu(uint16 screen_id, uint16 control_id, uint8 item, uint8 state)
{ (void)screen_id; (void)control_id; (void)item; (void)state; }
void NotifySelector(uint16 screen_id, uint16 control_id, uint8 item)
{ (void)screen_id; (void)control_id; (void)item; }
void NotifyTimer(uint16 screen_id, uint16 control_id)
{ (void)screen_id; (void)control_id; }
void NotifyReadFlash(uint8 status, uint8 *_data, uint16 length)
{ (void)status; (void)_data; (void)length; }
void NotifyWriteFlash(uint8 status) { (void)status; }
