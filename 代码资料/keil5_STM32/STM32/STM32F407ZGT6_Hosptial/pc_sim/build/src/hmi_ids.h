/*!
*  \file hmi_ids.h
*  \brief 大彩串口屏工程 SPO2_Hospital 的画面 ID / 控件 ID 表
*
*  数据来源：VisualTFT 工程 F:\VisualTFT\SPO2_Hospital
*      - SPO2_Hospital.tftprj 的 <Pages> 顺序  ->  画面 ID
*      - output_115200\DefineID.h  +  各 *.tft 中 <item id="..">  ->  控件 ID
*  本文件已按实际 .tft 逐条核对，勿凭记忆修改；改屏后必须重新对照。
*
*  画面顺序（与 <Pages> 严格对应，不可调换）：
*      0 主界面          1 趋势            2 报警记录        3 设置
*      4 暂停监护        5 报警与探头状态   6 清空报警记录
*
*  所有画面都有一组公共状态条控件：床位 90 / 探头 91 / 时钟 92(RTC)
*/
#ifndef __HMI_IDS_H
#define __HMI_IDS_H

/* ==================== 画面 ID ==================== */
#define SCR_MAIN        0       /* 主界面（实时测量）   */
#define SCR_TREND       1       /* 趋势                 */
#define SCR_ALARM       2       /* 报警记录             */
#define SCR_SETUP       3       /* 设置                 */
#define SCR_PAUSE       4       /* 暂停监护（弹窗）     */
#define SCR_PROBE       5       /* 报警与探头状态       */
#define SCR_CLEAR       6       /* 清空报警记录（弹窗） */

#define SCR_COUNT       7

/* ==================== 公共状态条（每页都有） ==================== */
#define COM_TXT_BED             90      /* "床位 01"            */
#define COM_TXT_PROBE_HEADER    91      /* "探头已接入"         */
#define COM_RTC_CLOCK           92      /* RTC 控件，屏自行刷新 */

/* ==================== 画面 0：主界面 ==================== */
/* 5 个按钮均为 button_type=0（瞬变）+ switch，按下后屏自动跳画面并向 MCU 上报通知 */
#define MAIN_BTN_PAUSE          1       /* -> 暂停监护  */
#define MAIN_BTN_TREND          2       /* -> 趋势      */
#define MAIN_BTN_ALARM          3       /* -> 报警记录  */
#define MAIN_BTN_SETUP          4       /* -> 设置      */
#define MAIN_BTN_PROBE          5       /* -> 报警与探头状态 */

#define MAIN_TXT_SPO2           10      /* SpO2 数值 "98" / "--"        */
#define MAIN_TXT_PR             11      /* 脉率数值 "72" / "--"         */
#define MAIN_TXT_VALID          12      /* "测量有效" / "信号无效"      */
#define MAIN_TXT_SPO2_LIMITS    13      /* "报警限值 90-100 %"          */
#define MAIN_TXT_SPO2_STATE     14      /* "稳定" / "血氧偏低"          */
#define MAIN_TXT_PR_LIMIT       15      /* "报警高限 120 bpm"           */
#define MAIN_TXT_PR_STATE       16      /* "脉搏规则" / "脉率偏快"      */
#define MAIN_TXT_QUALITY        17      /* 灌注质量 0~100               */
#define MAIN_IMG_QUALITY_ICON   18      /* 质量图标：图片控件，图片由屏固定，
                                            MCU 无对应切换指令，仅数值可写 */
#define MAIN_GRAPH_PPG          19      /* graph 曲线：宽742 高78 sample_count=300
                                            值域 0~255 单通道 -> 每个点 1 字节 */
#define MAIN_TXT_CAPTURE_STATE  20      /* "脉搏波采集中" / "监护已暂停" */
#define MAIN_TXT_DURATION       21      /* "监护时间 00:23:18"           */

/* ==================== 画面 1：趋势 ==================== */
/* 三个时段按钮为 button_type=1（自锁开关）：
   state=1 时显示 *_selected.png，state=0 显示 *_up.png
   -> 互斥选中态必须由 MCU 用 SetButtonValue() 驱动 */
#define TRD_BTN_RANGE_30        1       /* 30 分钟（设计默认选中） */
#define TRD_BTN_RANGE_2H        2       /* 2 小时                  */
#define TRD_BTN_RANGE_8H        3       /* 8 小时                  */
#define TRD_BTN_BACK            4       /* -> 主界面（屏自动跳）   */

#define TRD_TXT_LAST_SPO2       10      /* 最近 SpO2  "98"      */
#define TRD_TXT_LAST_PR         11      /* 最近脉率   "72"      */
#define TRD_GRAPH_SPO2          12      /* historygraph 量程 90~100
                                           sample_count=1024  sample_cycle=60s */
#define TRD_TXT_PERIOD          13      /* "记录时段 30 分钟"   */
#define TRD_GRAPH_PR            14      /* historygraph 量程 60~120 */
#define TRD_TXT_TIME_0          15      /* 时间刻度 0（最早）   */
#define TRD_TXT_TIME_1          16
#define TRD_TXT_TIME_2          17
#define TRD_TXT_TIME_3          18      /* 时间刻度 3（当前）   */

/* ==================== 画面 2：报警记录 ==================== */
#define ALM_BTN_CLEAR           1       /* -> 清空报警记录（屏自动跳） */
#define ALM_BTN_BACK            2       /* -> 主界面（屏自动跳）       */
#define ALM_BTN_ROW_1           3       /* 第 1 行 -> 报警与探头状态   */
#define ALM_BTN_ROW_2           4
#define ALM_BTN_ROW_3           5
#define ALM_BTN_ROW_4           6

#define ALM_TXT_EVENT_COUNT     10      /* "共 N 条记录"        */
#define ALM_TXT_EMPTY           11      /* "暂无报警记录"       */

/* 第 n 行（n = 1..4）的 4 个文本控件：
       时间 = ALM_ROW_TIME(n)  = 20 + (n-1)*4
       事件 = ALM_ROW_EVENT(n) = 21 + (n-1)*4
       级别 = ALM_ROW_LEVEL(n) = 22 + (n-1)*4
       状态 = ALM_ROW_STATE(n) = 23 + (n-1)*4                     */
#define ALM_ROW_TIME(n)         (20 + ((n) - 1) * 4)
#define ALM_ROW_EVENT(n)        (21 + ((n) - 1) * 4)
#define ALM_ROW_LEVEL(n)        (22 + ((n) - 1) * 4)
#define ALM_ROW_STATE(n)        (23 + ((n) - 1) * 4)

/* ==================== 画面 3：设置 ==================== */
#define SET_TXT_SPO2_LOW        1       /* 文本输入：SpO2 下限（弹键盘） */
#define SET_TXT_PR_HIGH         2       /* 文本输入：脉率上限（弹键盘） */
#define SET_BTN_SOUND           3       /* button_type=1 自锁：1=声音开   */
#define SET_BTN_BACK            4       /* -> 主界面（屏自动跳）          */
#define SET_BAR_BRIGHTNESS      10      /* 进度条 min=10 max=100          */
#define SET_SLIDER_BRIGHTNESS   11      /* 滑块   10~100                  */
#define SET_TXT_BRIGHTNESS      12      /* "80%"                          */
#define SET_TXT_SOUND_STATE     13      /* "声音提示已开启"               */

/* ==================== 画面 4：暂停监护 ==================== */
/* 三个按钮的 switch 都指向"主界面"（屏自动跳），MCU 只做业务处理 */
#define PAU_BTN_CANCEL          1       /* 取消：保持当前监护状态 */
#define PAU_BTN_CONFIRM         2       /* 确认：切换 监护/暂停   */
#define PAU_BTN_BACK            3
#define PAU_TXT_CONTEXT         10      /* 上下文提示文本 */

/* ==================== 画面 5：报警与探头状态 ==================== */
#define PRB_BTN_BACK            1       /* -> 主界面（屏自动跳） */
#define PRB_TXT_TITLE           10      /* "当前状态"                    */
#define PRB_TXT_NAME            11      /* 状态名：监护正常 / 信号偏弱… */
#define PRB_TXT_ADVICE          12      /* 处理建议                      */
#define PRB_TXT_PROBE           13      /* "已接入" / "未接入" / "信号差" */
#define PRB_TXT_UPDATE          14      /* "状态更新时间 HH:MM:SS"       */
#define PRB_TXT_QUALITY         15      /* 质量数值                      */
#define PRB_BAR_QUALITY         16      /* 进度条 0~100                  */
#define PRB_TXT_LAST_SPO2       17      /* "98 %"                        */
#define PRB_TXT_LAST_PR         18      /* "72 bpm"                      */
#define PRB_TXT_VALID_TIME      19      /* "最近有效时间 HH:MM:SS"       */
#define PRB_TXT_FOOTER          20      /* "等待稳定信号"                */

/* ==================== 画面 6：清空报警记录 ==================== */
#define CLR_BTN_CANCEL          1       /* -> 报警记录（屏自动跳） */
#define CLR_BTN_CONFIRM         2       /* 确认清空：MCU 擦除记录  */
#define CLR_BTN_BACK            3       /* -> 报警记录（屏自动跳） */
#define CLR_TXT_CONTEXT         10      /* 上下文提示文本          */

#endif /* __HMI_IDS_H */
