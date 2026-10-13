/*!
 * \file app_bloodoxygen.c
 * \brief 应用层：把 UNO 解析出的真实数据写到 SPO2_Hospital 屏上
 *
 * 与《代码开发设计步骤》一致：不再直读 SPI（那部分在 UNO 上完成），
 * 而是消费 UNO 解析结果 g_meas。时序：
 *   收到 UNO 帧 -> on_uno_frame() 推波形（约 90 帧/s）
 *   task_hmi_value() 每 400ms  写数值/状态 + 报警判定
 *   task_hmi_trend() 每 2s     追加一点到历史曲线（★2026-10-10 由 60s 改为 2s）
 *   task_clock()     每 1s     推进软 RTC + 累加监护时长并刷"监护时间"
 *
 * ★ 控件归属（2026-10-10 **二次更正**）：
 *   上一版判断"屏端 main.lua 不会执行"是【错的】，据此产生的 Lua 独占结论已作废。
 *   实际证据：
 *     1) 每次编译都会把工程根目录的 main.lua 原样拷进 dciot_build\private\main.lua
 *        （两者 md5 完全相同），即 Lua 脚本是随工程下发到屏上的；
 *     2) tftprj 里 LUAPrecompile="0"，该项在 VisualTFT 里的中文名正是"**预编译LUA**"
 *        （勾选框），0 = 不在上位机预编译 —— 由屏上电后自行解释 main.lua。
 *   而 main.lua 与 MCU 双写同一批控件、且各自维护一份 paused / monitor_sec，
 *   正是"暂停后按钮文案不变 / 监护时间永远是 00:00:01"的根因：
 *     - main.lua on_init: start_timer(0,1000,0,1)，repeat=1 **只触发一次**，
 *       on_timer 把 monitor_sec 加到 1 后再也不触发；而每次切回主界面
 *       on_screen_change -> refresh_main() 都写 "监护时间 "..fmt_hms(1) == 00:00:01；
 *     - refresh_main() 用 Lua 自己的 paused(=false) 把 12/20 覆盖回
 *       "测量有效 / 脉搏波采集中"，把 MCU 写的"已暂停 / 暂停中"冲掉。
 *   => 处理办法：**屏端 main.lua 已停用**（换成空壳脚本，原版备份为
 *      main.lua.原版备份.txt），全部动态内容改由 MCU **独占**写入（见 hmi_ids.h）。
 *
 * ★ 画面跳转由屏原生完成：button_type=0 的按钮其 switch 属性就是目标页面名，
 *   点击后屏自己跳转，MCU 不需要发 EE B1 00。
 *
 * ★ 中文字符串：AC5 不允许字符串字面量含非 ASCII（#870-D），故屏上要显示的
 *   汉字一律用 GBK 的 \xNN 转义，见下面 S_* 宏。
 */

#include "dc_hmi.h"
#include "hmi_ids.h"
#include "uno.h"
#include "sys.h"        /* millis() */
#include <stdio.h>
#include <stdlib.h>     /* atoi */
#include <string.h>

extern measurement_t g_meas;
extern uint8_t      g_new_frame;

/* ============================================================
 * 0. 屏上要显示的汉字（GBK 转义，AC5 下不能直接写中文）
 *    ★ 每个宏都以 GBK 双字节结尾，绝不紧跟十六进制数字，
 *      避免 AC5 的 \x 转义"贪心多吃一位"（如 "\xE4" "0" 必须拆开写）。
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
 *   （2 秒/点：30 点=1 分钟、150 点=5 分钟、900 点=30 分钟） */
#define S_TRD_1MIN    "\xBC\xC7\xC2\xBC\xCA\xB1\xB6\xCE 1 \xB7\xD6\xD6\xD3"   /* 记录时段 1 分钟 */
#define S_TRD_5MIN    "\xBC\xC7\xC2\xBC\xCA\xB1\xB6\xCE 5 \xB7\xD6\xD6\xD3"   /* 记录时段 5 分钟 */
#define S_TRD_30MIN   "\xBC\xC7\xC2\xBC\xCA\xB1\xB6\xCE 30 \xB7\xD6\xD6\xD3"  /* 记录时段 30 分钟 */

/* ---- 暂停/监护状态文案（本次新增，原指望 Lua，现由 MCU 写） ---- */
#define S_VALID_OK    "\xB2\xE2\xC1\xBF\xD3\xD0\xD0\xA7"               /* 测量有效 */
#define S_VALID_PAUSE "\xD2\xD1\xD4\xDD\xCD\xA3"                       /* 已暂停   */
#define S_CAP_ON      "\xC2\xF6\xB2\xAB\xB2\xA8\xB2\xC9\xBC\xAF\xD6\xD0" /* 脉搏波采集中 */
#define S_CAP_PAUSE   "\xD4\xDD\xCD\xA3\xD6\xD0"                       /* 暂停中   */
/* "监护时间"前缀，后面用 "%s %02u:%02u:%02u" 拼，故宏内不带空格与数字 */
#define S_DUR_PFX     "\xBC\xE0\xBB\xA4\xCA\xB1\xBC\xE4"               /* 监护时间 */
/* 报警限值前缀 */
#define S_LIM_SPO2    "\xB1\xA8\xBE\xAF\xCF\xDE\xD6\xB5"               /* 报警限值 */
#define S_LIM_PR      "\xB1\xA8\xBE\xAF\xB8\xDF\xCF\xDE"               /* 报警高限 */
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

static uint8_t s_sound_on = 1;        /* 报警声音开关（设置页切换按钮） */
static uint8_t s_bright_pct = 80;     /* 背光百分比（设置页滑块） */
static int16_t s_spo2_low = 90;       /* SpO2 报警下限，可由设置页修改 */
static int16_t s_pr_high  = 120;      /* 脉率报警上限，可由设置页修改 */

/* 监护时长（秒）：正常监护时每秒 +1，暂停时冻结 */
static uint32_t s_monitor_sec = 0;

/* 上一次写进屏的监护时长文本，只有整秒变化才发帧，省带宽 */
static uint32_t s_dur_shown = 0xFFFFFFFFu;

/* 上一次写进屏的报警限值，只有设置页改阈值后才重发 */
static int16_t  s_lim_spo2_shown = -1;
static int16_t  s_lim_pr_shown   = -1;

/* 上次下发过的"暂停界面"状态（0xFF=还没下发过） */
static uint8_t  s_pause_ui_shown = 0xFF;

/* ---- 软 RTC（与血压工程同源做法：MCU 自己维护年月日时分秒） ----
   ★ 起始时间：改这三行即可对上真实日期（上电后从 00:00:00 开始走）。 */
#define RTC_INIT_YEAR   2026
#define RTC_INIT_MON    1
#define RTC_INIT_DAY    1

static uint16_t s_year = RTC_INIT_YEAR;
static uint8_t  s_mon  = RTC_INIT_MON;
static uint8_t  s_day  = RTC_INIT_DAY;
static uint32_t s_sec_of_day = 0;          /* 0~86399 */
static uint32_t s_rtc_last_tick = 0;       /* 上次推进用的 millis() 基准（保留余数） */
static uint32_t s_rtc_last_sync = 0;       /* 上次同步到屏的时刻 */

/* ---- 报警记录 ---- */
#define ALM_ROWS      4u
#define ALM_HOLD_MS   3000u     /* 越限持续多久才记一条，防抖动 */
#define ALM_REL_SPO2  92        /* 解除阈值（滞回）：>= 92 才解除 */
#define ALM_REL_PR    115       /* <= 115 才解除 */

typedef struct {
    char tm[9];                 /* "HH:MM:SS" */
    char ev[26];                /* 事件，如 "血氧 88%" */
    char lv[8];                 /* 级别：高 / 中 */
    char st[16];                /* 状态：未处理 */
} alm_rec_t;

static alm_rec_t s_recs[ALM_ROWS];      /* [0] 为最新 */
static uint8_t   s_rec_cnt = 0;
static uint16_t  s_event_total = 0;

/* [0]=SpO2 [1]=脉率 */
static uint8_t  s_alm_active[2];
static uint8_t  s_alm_pending_on[2];
static uint32_t s_alm_pending_at[2];

/* 探头状态条上次写过的值（0xFF = 还没写过），只在变化时写，省 7 帧带宽 */
static uint8_t  s_probe_shown = 0xFF;

/* ============================================================
 * 1.5 屏文案刷新（这些过去指望屏端 Lua，现全部由 MCU 写）
 * ============================================================ */

/* 主界面右下角"监护时间 HH:MM:SS"
 *   force=0 时只在整秒变了才发帧（每秒调用 → 正常情况每秒 1 帧） */
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

/* 暂停/恢复界面：按钮显隐 + 两句状态文案 + 弹窗提示语
 *   暂停 -> 藏起"暂停监护"(1)、亮出"继续监护"(6)
 *   恢复 -> 反过来
 *   force=1 用于刚切回主界面（屏已按默认属性重绘）之后强制重发一次 */
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

/* 设置页：亮度百分比 + 声音提示状态 + 进度条/滑块回写
 *   ★ 2026-10-10：进度条/滑块的回写原先由屏端 main.lua 的 refresh_setup() 做，
 *     现已停用 Lua，改由 MCU 写，否则切到设置页时滑块位置与百分比文本会不一致。 */
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

    /* 每 5 分钟把 MCU 时间同步给屏（屏 RTC 有独立晶振，偶尔校准一下即可） */
    if ((now - s_rtc_last_sync) >= 300000u || s_rtc_last_sync == 0u) {
        s_rtc_last_sync = now;
        HMI_SetRTCTime((uint8_t)(s_year % 100u), s_mon, s_day,
                       (uint8_t)(s_sec_of_day / 3600u),
                       (uint8_t)((s_sec_of_day / 60u) % 60u),
                       (uint8_t)(s_sec_of_day % 60u),
                       rtc_weekday(s_year, s_mon, s_day));
    }

    /* ---- 监护时长：正常监护时随秒累加，暂停时冻结 ----
       这就是原来"监护时间一直停在 1 秒"的正解：
       过去靠屏端 Lua 的定时器累加，而 start_timer(...,repeat=1) 只触发一次；
       现在改由 MCU 每秒写 txt_duration(21)。 */
    if (!g_paused) s_monitor_sec += add;
    hmi_show_duration(0u);
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
    HMI_SetText(SCR_ALARM, ALM_TXT_EMPTY, (s_rec_cnt == 0u) ? S_NO_REC : "");
}

/* 产生一条记录（插到最前，只保留 4 条） */
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

/* 单个通道的越限判定：持续 hold 才记一条；回到解除阈值才解除 */
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

    /* 暂停监护期间不做报警判定（数值已显示 --），状态文案回到"正常" */
    if (g_paused) {
        HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2_ST, S_NORMAL);
        HMI_SetText(SCR_MAIN, MAIN_TXT_PR_ST,   S_NORMAL);
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
 * ============================================================ */
void on_uno_frame(void)
{
    if (g_paused) return;                        /* 暂停时不推波形 */
    HMI_WavePush((int32_t)(g_meas.ir_raw >> 8));
}

/* ============================================================
 * 5. 数值 + 状态刷新（每 400ms）
 *    暂停时不再"什么都不刷"：数值显示 --，状态文案与时长继续维持，
 *    避免屏上残留暂停前的旧数字。
 * ============================================================ */
static uint32_t t_val = 0;

/* 暂停时冻结显示的“最后一次有效”血氧/脉率：仅在“未暂停且数据有效”时刷新，
   g_paused 期间保持不变，从而让主界面（及趋势页最近值）在暂停时保留暂停前数值，
   恢复后再随实测数据变化。 */
static int16_t s_frozen_spo2 = -1;
static int16_t s_frozen_pr  = -1;

/* 切回某画面后，屏会按控件默认属性重绘，需要把 MCU 负责的内容再补几次。
   s_resync_round>0 表示"还要重发几轮"，避免和屏重绘抢时序。
   ★ 2026-10-10 起屏端 main.lua 已停用（它原先会用自己的 paused/monitor_sec
     覆盖 12/20/21 等控件），这里同时加了一道"周期性自愈补发"，
     保证任何一次覆盖/丢失都能在 RESYNC_SELF_HEAL_MS 内自动收敛回来。 */
static uint8_t  s_resync_round = 0;
static uint32_t s_resync_at    = 0;
static uint8_t  s_resync_alm   = 0;   /* 本轮补发是否连报警记录页一起补 */
static uint8_t  s_force_probe  = 0;   /* 强制下一轮把"探头已接入"整轮重写 */

#define RESYNC_SELF_HEAL_MS  3000u

/* 定义在第 6 节（趋势），这里先声明，供切页补发时调用 */
static void trend_show_labels(uint8_t force);

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
        s_force_probe = 1u;
    }

    if (now < t_val) return;
    t_val = now + 400u;

    /* 仅在“未暂停且数据有效”时刷新冻结缓存；暂停期间缓存保持不变 */
    if (!g_paused && g_meas.result_valid) {
        s_frozen_spo2 = (int16_t)g_meas.spo2;
        s_frozen_pr  = (int16_t)g_meas.pulse_rate;
    }

    if (g_paused) {
        /* 暂停：保留暂停前的最后一次有效值（不再显示 --） */
        if (s_frozen_spo2 >= 0) {
            snprintf(s[0], sizeof(s[0]), "%d", (int)s_frozen_spo2);
            snprintf(s[1], sizeof(s[1]), "%d", (int)s_frozen_pr);
        } else {
            strcpy(s[0], "--");
            strcpy(s[1], "--");
        }
    } else if (!g_meas.result_valid) {
        strcpy(s[0], "--");
        strcpy(s[1], "--");
    } else {
        snprintf(s[0], sizeof(s[0]), "%d", (int)g_meas.spo2);
        snprintf(s[1], sizeof(s[1]), "%d", (int)g_meas.pulse_rate);
    }
    snprintf(s[2], sizeof(s[2]), "%d", (int)g_meas.signal_quality);

    HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2,    s[0]);
    HMI_SetText(SCR_MAIN, MAIN_TXT_PR,      s[1]);
    HMI_SetText(SCR_MAIN, MAIN_TXT_QUALITY, s[2]);

    /* 探头状态页的质量进度条 */
    HMI_SetProgress(SCR_PROBE, PRB_BAR_QUALITY,
                    g_paused ? 0u : (uint32_t)g_meas.signal_quality);

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
 * ★ 2026-10-10 修复"趋势曲线是一条死直线，数据是死的"
 *   根因：屏 趋势.tft 里两个 historygraph 的 sample_data_foramt="0"
 *         = UINT8（每点 1 字节），而这里用的是 HMI_HistorySampleU16()，
 *         每调用一次发 2 字节（EE B1 60 <screen:16> <ctrl:16> <value:16>）。
 *         屏按 1 字节解析时只吃掉高字节 —— SpO2=97 即 0x0061，屏只取 0x00，
 *         于是曲线恒为 0、贴着量程下限画成一条直线；残余的 0x61 也不是 0xEE，
 *         被当噪声丢掉。
 *   修法：把 趋势.tft 的 sample_data_foramt 改成 2（UINT16），与下面的
 *         HMI_HistorySampleU16() 对齐（改屏工程需在 VisualTFT 重新编译下载）。
 *   顺带：采样节奏 60s -> 2s（屏 sample_cycle 也同步改成 2），
 *         三个时段按钮由 30分钟/2小时/8小时 改成 1分钟/5分钟/30分钟。
 * ============================================================ */
#define TREND_PERIOD_MS   2000u   /* 落点间隔，必须与 .tft 的 sample_cycle 一致 */
#define TREND_SAMPLE_SEC  2u      /* 一个点代表多少秒，用于时间刻度换算 */

static uint32_t t_trd = 0;

static uint16_t s_trd_count = 150u;   /* 当前窗口点数：30 / 150 / 900 */
static uint8_t  s_trend_sel = 2u;     /* 1=1分钟 2=5分钟 3=30分钟（按钮选中态） */
static uint32_t s_trd_min_shown = 0xFFFFFFFFu;

/* 趋势页的"非曲线"文案：左列最近值 10/11 + 曲线下方时间刻度 15..18。
 * force=1 无条件整轮重发（切到趋势页 / 切换时段后屏会按默认属性重绘，必须补）。 */
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
    } else if (!g_meas.result_valid) {
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
    if (!g_meas.result_valid) return;  /* 无效时不留洞 */

    v[0] = (uint16_t)g_meas.spo2;
    v[1] = (uint16_t)g_meas.pulse_rate;
    HMI_HistorySampleU16(SCR_TREND, TRD_GRAPH_SPO2, &v[0], 1);
    HMI_HistorySampleU16(SCR_TREND, TRD_GRAPH_PR,   &v[1], 1);
}

/* ============================================================
 * 7. 屏按键 / 滑块 / 文本 回调（由 dc_hmi.c 的 HMI_Poll() 调进来）
 *
 * ★ 2026-10-10 重要修正 —— 三类按钮的配置约定（依据官方例程
 *   Project_DEMO_800480M070 与 VisualTFT 内 button_type 枚举）：
 *
 *   button_type  OPTIONS="切换画面;开关描述;自定义按键;自定义指令;弹出菜单;" ENUMS="0;1;2;3;4;"
 *     =0 切换画面：屏按 switch 属性自己跳页；本工程实测**不会**可靠上报按键事件
 *     =1 开关描述：只上报事件、不跳页（switch 必须留空）——需要 MCU 干活的按钮都用它
 *   button_style OPTIONS="瞬变;开关;置位;复位;长按;延时;" ENUMS="0;1;2;3;4;5;"
 *     =0 瞬变（点动按钮用这个）/ =1 开关（切换型按钮用这个）
 *   notify_disable OPTIONS="否;是" ENUMS="1;0"   -> 0 = 上传按键事件
 *
 *   本工程 tftprj 里 DeviceControlNotify="3" = "按下和弹起都上报"，
 *   所以**一次点击会收到两帧**（state=1 按下、state=0 弹起）——
 *   凡是"翻转型"动作都必须只在 state==1 时执行，否则一次点击会翻转两次（净变化为 0）。
 *
 *   跳页分工：btn_confirm_pause / btn_confirm_clear 已改成 button_type=1、switch="",
 *   所以**跳页必须由 MCU 收到通知后自己发 EE B1 00**；取消/返回按钮仍是
 *   button_type=0 原生跳页，作为"卡在弹窗页"的兜底出口。
 * ============================================================ */

/* 安排若干轮"MCU 负责内容"的补发（切页/跳页后屏会按控件默认属性重绘，必须补） */
static void request_resync(uint8_t rounds, uint16_t delay_ms)
{
    s_resync_round = rounds;
    s_resync_at    = millis() + delay_ms;
}

/* 切换趋势窗口：count = 一屏显示的采样点数（EE B1 63）。
 *   2 秒/点 -> 30 点=1 分钟、150 点=5 分钟、900 点=30 分钟（屏缓存 1024 点）。
 *   三个时段按钮是"开关型"（button_style=1），选中态原先由屏端 Lua 的
 *   refresh_trend() 设置；Lua 已停用，改由 MCU 用 EE B1 10 <u8> 回写。 */
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
    /* ★ 必须先判边沿：DeviceControlNotify=3 -> 一次点击上报"按下"与"弹起"两帧。
       若把翻转型动作放在边沿判断之前，声音开关这类按钮会被翻转两次（等于没反应）。 */
    if (!state) return;                   /* 只处理按下沿 */

    /* 设置页"声音提示"：切换型按钮（button_style=1），按一次翻转一次 */
    if (screen_id == SCR_SETUP && ctrl_id == SET_BTN_SOUND) {
        s_sound_on = (uint8_t)(!s_sound_on);
        hmi_show_setup();                 /* 同步"声音提示已开启/已关闭" */
        return;
    }

    if (screen_id == SCR_MAIN && ctrl_id == MAIN_BTN_RESUME) {
        /* "继续监护"：button_type=1(只上报) + switch=""，跳页不需要（本来就在主界面） */
        g_paused = 0;
        hmi_show_pause_ui(1u);
        return;
    }

    if (screen_id == SCR_PAUSE && ctrl_id == PAU_BTN_CONFIRM) {
        /* 弹窗"确认"：在暂停与恢复之间翻转。
           该按钮已改成 button_type=1 + switch=""，屏**不会**自己跳页，
           所以这里必须由 MCU 跳回主界面；先跳页再补发文案（屏重绘会清掉 MCU 写的内容）。 */
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

/* 画面切换回调。
 * ★ 2026-10-10 修正：dc_hmi.c 里 HMI_Poll() 的帧长门限原来是 11 字节，
 *   而"画面切换通知"帧只有 9 字节（EE B1 01 <screen:16> FF FC FF FF），
 *   会被整条丢掉 —— 本函数因此从来没被调用过。门限已改为 9。
 * 屏每次跳转都会按控件默认属性重绘，所以这里安排"MCU 负责内容"的整体补发。 */
void NotifyScreen(uint16_t screen_id)
{
    if (screen_id == SCR_MAIN || screen_id == SCR_PAUSE || screen_id == SCR_SETUP ||
        screen_id == SCR_TREND || screen_id == SCR_ALARM || screen_id == SCR_PROBE) {
        request_resync(4u, 120u);
    }
    if (screen_id == SCR_ALARM) s_resync_alm = 1u;   /* 报警记录页顺带补 4 行文本 */
    if (screen_id == SCR_SETUP) hmi_show_setup();
}

/* 设置页滑块：面板亮度。
 *   ★ 这里**只**写亮度百分比文本，不调用 hmi_show_setup()：
 *     hmi_show_setup() 会把滑块/进度条的值写回屏，若屏对"程序设值"也回通知，
 *     就会形成 滑块通知 -> 设滑块 -> 再通知 的死循环。文本控件无此风险。 */
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

/* 设置页文本输入：SpO2 下限 / 脉率上限 -> 更新 MCU 的报警阈值 */
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

    /* 初始背光 80%（与屏 Lua 的设置页默认值一致） */
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

    /* 设置屏 RTC：马上同步一次（软 RTC 从 RTC_INIT_* 开始走） */
    s_rtc_last_tick = millis();
    s_rtc_last_sync = 0u;      /* 让 task_clock 的首次调用立即同步 */
    HMI_SetRTCTime((uint8_t)(s_year % 100u), s_mon, s_day,
                   (uint8_t)(s_sec_of_day / 3600u),
                   (uint8_t)((s_sec_of_day / 60u) % 60u),
                   (uint8_t)(s_sec_of_day % 60u),
                   rtc_weekday(s_year, s_mon, s_day));

    /* ---- 下面这些过去由屏端 Lua 写，现在必须由 MCU 在开机时铺一遍 ---- */
    hmi_show_pause_ui(1u);    /* ① 藏 btn_resume、亮 btn_pause  ② 12/20 文案 ③ 时长 */
    hmi_show_limits(1u);      /* 主界面 13/15 报警限值文案 */
    hmi_show_setup();         /* 设置页 12 亮度百分比 / 13 声音提示状态 / 滑块回写 */
    hmi_show_duration(1u);    /* 主界面 21 监护时间 00:00:00 */
    s_resync_round = 0u;      /* 开机时不需要补发轮次 */
    s_resync_alm   = 0u;
    s_force_probe  = 0u;
}
