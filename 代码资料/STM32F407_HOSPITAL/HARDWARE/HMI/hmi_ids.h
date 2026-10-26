/*!
 * \file hmi_ids.h
 * \brief SPO2_Hospital 工程：画面 ID 与控件 ID
 *
 * 数据来源：<工程目录>\output\DefineID.h，由 VisualTFT 编译导出，勿手改。
 * 页面顺序与 SPO2_Hospital.tftprj 中 <Pages> 的顺序严格对应：
 *   0 主界面 / 1 趋势 / 2 报警记录 / 3 设置 / 4 暂停监护 / 5 报警与探头状态 / 6 清空报警记录
 *
 * 画面 ID 发生变化时，必须重新编译工程并同步本文件。
 *
 * 控件归属约定：
 *   屏工程根目录的 main.lua 会在屏内执行，并与 MCU 抢写同一批控件，
 *   故已替换为空壳脚本（原版备份 main.lua.原版备份.txt），
 *   全部动态内容改由 MCU 独占写入；否则会出现监护时间恒为 00:00:01、
 *   暂停后按钮文案不变等现象。
 *
 *   【MCU 负责写】：
 *     MAIN_TXT_SPO2(10) MAIN_TXT_PR(11) MAIN_TXT_QUALITY(17)
 *     MAIN_TXT_VALID(12)      —— "测量有效" / "已暂停"
 *     MAIN_TXT_SPO2_LIM(13)   —— "报警限值 90-100 %"（随设置页下限联动）
 *     MAIN_TXT_SPO2_ST(14) MAIN_TXT_PR_ST(16)  —— 正常 / 血氧偏低 / 脉率偏快
 *     MAIN_TXT_PR_LIM(15)     —— "报警高限 120 bpm"（随设置页上限联动）
 *     MAIN_TXT_CAP_ST(20)     —— "脉搏波采集中" / "暂停中"
 *     MAIN_TXT_DURATION(21)   —— "监护时间 HH:MM:SS"（每秒刷新，暂停冻结）
 *     MAIN_TXT_PROBE(91)  MAIN_GRAPH_PPG(19)
 *     MAIN_BTN_PAUSE(1)/MAIN_BTN_RESUME(6) 的显隐（EE B1 03）
 *     趋势 TRD_GRAPH_SPO2(12)/TRD_GRAPH_PR(14) TRD_TXT_PERIOD(13)
 *     报警记录页 SCR_ALARM 的 4 行文本 + 条数(10) + 空态(11)
 *     探头状态页 PRB_TXT_PROBE(13) PRB_BAR_QUALITY(16)
 *     设置页 SET_TXT_BRIGHT(12) SET_TXT_SOUND_ST(13)
 *     暂停弹窗 PAU_TXT_CONTEXT(10)
 *     屏 RTC —— 用 EE 81 指令设置（控件 92 是 type="rtc"，写文本无效）
 *
 *   按钮配置约定（依据 VisualTFT 内 button_type 枚举 + 官方例程）：
 *     button_type  OPTIONS="切换画面;开关描述;自定义按键;自定义指令;弹出菜单;"
 *                  ENUMS="0;1;2;3;4;"
 *       =0 切换画面：屏按 switch 属性自己跳页；实测不可靠上报按键事件
 *       =1 开关描述：只上报事件、不跳页（switch 必须留空）—— 要 MCU 干活的按钮都用它
 *     button_style OPTIONS="瞬变;开关;置位;复位;长按;延时;" ENUMS="0;1;2;3;4;5;"
 *       =0 瞬变（点动按钮）/ =1 开关（切换型按钮，如声音提示、时段选择）
 *     notify_disable OPTIONS="否;是" ENUMS="1;0"  -> 0 = 上传按键事件（要留 0）
 *
 *   本工程 tftprj: DeviceControlNotify="3" = "按下和弹起都上报"，
 *   所以一次点击会收到两帧（state=1 按下 / state=0 弹起）。
 *   凡翻转型动作必须只在 state==1 时执行，否则一次点击翻转两次 = 没反应。
 *
 *   分工：
 *     btn_pause(主界面) / 取消键 / 返回键 / 报警记录的 4 行 = button_type=0 原生跳页；
 *     btn_resume(6) / btn_confirm_pause(2) / btn_confirm_clear(2) /
 *     声音提示(3) / 时段 1/2/3 = button_type=1，只上报，跳页由 MCU 发 EE B1 00。 */

#ifndef __HMI_IDS_H
#define __HMI_IDS_H

/* ============================ 画面 ID ============================ */
#define SCR_MAIN        0   /* 主界面（实时测量）   */
#define SCR_TREND       1   /* 趋势                 */
#define SCR_ALARM       2   /* 报警记录             */
#define SCR_SETUP       3   /* 设置                 */
#define SCR_PAUSE       4   /* 暂停监护（弹窗）     */
#define SCR_PROBE       5   /* 报警与探头状态       */
#define SCR_CLEAR       6   /* 清空报警记录（弹窗） */

/* ============================ 主界面 (0) ============================ */
/* 按钮：button_type=0 的按键屏按 switch 自己跳页；button_type=1 的只上报事件 */
#define MAIN_BTN_PAUSE      1   /* switch="暂停监护" -> SCR_PAUSE，type=0 原生跳页 */
#define MAIN_BTN_RESUME     6   /* "继续监护"，与 btn_pause 同位置叠放，type=1 只上报 */
#define MAIN_BTN_TREND      2   /* switch="趋势"      -> SCR_TREND   */
#define MAIN_BTN_ALARM      3   /* switch="报警记录"  -> SCR_ALARM   */
#define MAIN_BTN_SETUP      4   /* switch="设置"      -> SCR_SETUP   */
#define MAIN_HOT_PROBE      5   /* switch="报警与探头状态" -> SCR_PROBE */

/* 文本显示 */
#define MAIN_TXT_SPO2       10  /* SpO2 数值，如 "98"     [MCU] */
#define MAIN_TXT_PR         11  /* PR 脉率，如 "72"       [MCU] */
#define MAIN_TXT_VALID      12  /* "测量有效"/"已暂停"    [MCU] */
#define MAIN_TXT_SPO2_LIM   13  /* "报警限值 90-100 %"    [MCU] */
#define MAIN_TXT_SPO2_ST    14  /* "正常"/"血氧偏低"      [MCU] */
#define MAIN_TXT_PR_LIM     15  /* "报警高限 120 bpm"     [MCU] */
#define MAIN_TXT_PR_ST      16  /* "正常"/"脉率偏快"      [MCU] */
#define MAIN_TXT_QUALITY    17  /* 灌注质量 0~100         [MCU] */
#define MAIN_TXT_QUALITY_LABEL 93 /* "信号质量"静态标签，覆盖背景图中缺字旧标签 [静态] */
#define MAIN_TXT_CAP_ST     20  /* "脉搏波采集中"/"暂停中" [MCU] */
#define MAIN_TXT_DURATION   21  /* "监护时间 HH:MM:SS"    [MCU，每秒刷新] */

/* 曲线控件：宽 742px，sample_count=300，值域 0~255，单通道，每个数据点 1 字节 */
#define MAIN_GRAPH_PPG      19

/* 公共状态条 */
#define MAIN_TXT_BED        90  /* "床位 01"              [静态] */
#define MAIN_TXT_PROBE      91  /* "探头已接入/未接入"    [MCU] */
#define MAIN_RTC_CLOCK      92  /* type="rtc"，屏内部时钟 [用 EE 81 设 RTC，不能写文本] */

/* ============================ 趋势 (1) ============================ *
 * historygraph 配置：sample_data_foramt=2(UINT16/每点 2 字节)，与 HMI_HistorySampleU16() 对齐；
 * sample_cycle=2s，曲线每 2 秒落一个点。
 * 三个时段按钮：2 秒/点 → 30 点=1 分钟、150 点=5 分钟、900 点=30 分钟，均在 1024 点缓存内。
 * 按钮文字烤在 PNG 里，图片由 tools/gen_range_buttons.py 生成。 */
#define TRD_BTN_1MIN        1   /* 开关型按钮，选中态由 MCU 用 EE B1 10 回写 */
#define TRD_BTN_5MIN        2
#define TRD_BTN_30MIN       3
#define TRD_BTN_BACK        4   /* button_type=0，屏原生跳回主界面 */
#define TRD_TXT_LAST_SPO2   10  /* 左列"最近值"（大号字），由 MCU 写 [MCU] */
#define TRD_TXT_LAST_PR     11
#define TRD_GRAPH_SPO2      12  /* historygraph, UINT16, sample_cycle=2s, 量程 90~100 */
#define TRD_TXT_PERIOD      13  /* "记录时段 5 分钟"，由 MCU 回写 */
#define TRD_GRAPH_PR        14  /* historygraph, UINT16, sample_cycle=2s, 量程 60~120 */
#define TRD_TXT_TIME_0      15  /* 时间刻度 0~3（曲线下方），由 MCU 写 [MCU] */
#define TRD_TXT_TIME_1      16
#define TRD_TXT_TIME_2      17
#define TRD_TXT_TIME_3      18

/* ============================ 报警记录 (2) ============================ */
#define ALM_BTN_CLEAR       1   /* -> SCR_CLEAR */
#define ALM_BTN_BACK        2
#define ALM_ROW_1           3
#define ALM_ROW_2           4
#define ALM_ROW_3           5
#define ALM_ROW_4           6
#define ALM_TXT_EVENT_CNT   10
#define ALM_TXT_EMPTY       11
/* 第 n 行（n=1..4）的四个文本控件：
 *     时间 = ALM_ROW_TIME(n)     = 20 + (n-1)*4
 *     事件 = ALM_ROW_EVENT(n)    = 21 + (n-1)*4
 *     级别 = ALM_ROW_LEVEL(n)    = 22 + (n-1)*4
 *     状态 = ALM_ROW_STATE(n)    = 23 + (n-1)*4
 */
#define ALM_ROW_TIME(n)     (20 + ((n) - 1) * 4)
#define ALM_ROW_EVENT(n)    (21 + ((n) - 1) * 4)
#define ALM_ROW_LEVEL(n)    (22 + ((n) - 1) * 4)
#define ALM_ROW_STATE(n)    (23 + ((n) - 1) * 4)

/* ============================ 设置 (3) ============================ */
#define SET_TXT_SPO2_LOW    1   /* 文本输入：SpO2 下限 */
#define SET_TXT_PR_HIGH     2   /* 文本输入：PR 上限   */
#define SET_BTN_SOUND       3   /* 开关：声音提示      */
#define SET_BTN_BACK        4
#define SET_BAR_BRIGHT      10  /* 进度条 */
#define SET_SLIDER_BRIGHT   11  /* 滑块（ID = 进度条 ID + 1） */
#define SET_TXT_BRIGHT      12  /* "80%"，随滑块联动，由 MCU 写 */
#define SET_TXT_SOUND_ST    13  /* "声音提示已开启/关闭"，由 MCU 写 */

/* ============================ 暂停监护 (4) ============================ */
/* btn_confirm_pause 为 button_type=1 + button_style=0 + switch=""（只上报、不跳页），
 * 跳回主界面由 MCU 收到通知后发 EE B1 00 完成。
 * 取消/返回键保持 button_type=0 原生跳页，作为"卡在弹窗页"的兜底出口。 */
#define PAU_BTN_CANCEL      1   /* switch="主界面"，type=0 原生跳页，不改状态 */
#define PAU_BTN_CONFIRM     2   /* type=1(只上报)；MCU 翻转 g_paused + 发 EE B1 00 跳页 */
#define PAU_BTN_BACK        3   /* switch="主界面"，type=0 原生跳页 */
#define PAU_TXT_CONTEXT     10  /* 弹窗提示语，由 MCU 按当前状态写 */

/* ============================ 报警与探头状态 (5) ============================ */
#define PRB_BTN_BACK        1
#define PRB_TXT_TITLE       10
#define PRB_TXT_NAME        11
#define PRB_TXT_ADVICE      12
#define PRB_TXT_PROBE       13
#define PRB_TXT_UPD_TIME    14
#define PRB_TXT_QUALITY     15
#define PRB_BAR_QUALITY     16  /* 进度条 0~100（宽进度条给"量"；与下方 6 段图标互补） */

/* 信号质量柱状图（6 段 RAG，颜色已烤进 PNG）：主界面(SCR_MAIN) 与探头状态页(SCR_PROBE)
 * 各放一套叠放的 image 控件，ID 同为 50~55，按画面 SCR 区分；MCU 用 HMI_SetControlVisible
 * 只显示当前档位那一张、隐藏其余（quality_0 = 全暗 = 全灭）。
 *   0=全暗 1=红 2=红 3=黄 4=绿 5=全绿；SQI 每 20% 一档（见 app_bloodoxygen.c sqi_to_seg）。
 * 驱动无 HMI_SetIcon，故用"图标显隐"换色——.tft 里 6 张叠放在同一坐标即可。 */
#define MAIN_SQI_ICON0  50
#define MAIN_SQI_ICON1  51
#define MAIN_SQI_ICON2  52
#define MAIN_SQI_ICON3  53
#define MAIN_SQI_ICON4  54
#define MAIN_SQI_ICON5  55
#define PRB_SQI_ICON0  50
#define PRB_SQI_ICON1  51
#define PRB_SQI_ICON2  52
#define PRB_SQI_ICON3  53
#define PRB_SQI_ICON4  54
#define PRB_SQI_ICON5  55
#define PRB_TXT_LAST_SPO2   17
#define PRB_TXT_LAST_PR     18
#define PRB_TXT_VALID_TIME  19
#define PRB_TXT_FOOTER      20

/* ============================ 清空报警记录 (6) ============================ */
/* btn_confirm_clear 为 button_type=1 + switch=""，跳回"报警记录"页由 MCU 收到通知后发 EE B1 00 完成。 */
#define CLR_BTN_CANCEL      1   /* switch="报警记录"，type=0 原生跳页 */
#define CLR_BTN_CONFIRM     2   /* type=1(只上报)；MCU 清记录 + 发 EE B1 00 跳页 */
#define CLR_BTN_BACK        3   /* switch="报警记录"，type=0 原生跳页 */
#define CLR_TXT_CONTEXT     10

#endif /* __HMI_IDS_H */
