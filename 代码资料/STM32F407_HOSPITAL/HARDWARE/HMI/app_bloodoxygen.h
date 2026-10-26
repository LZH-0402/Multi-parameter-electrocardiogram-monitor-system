#ifndef __APP_BO_H
#define __APP_BO_H

#include <stdint.h>

/* 血氧屏应用层：把 UNO 解析出的测量数据搬到屏幕上。
   裸机：用 SysTick 提供的 millis() 驱动；RTOS：把各节拍各自做成任务即可。
   屏端 main.lua 已停用（换成空壳脚本，原版备份 main.lua.原版备份.txt），
   全部动态内容由 MCU 独占写入，控件归属如下：

     MCU 负责写：
       主界面  10/11(SpO2、PR) 12(测量有效/已暂停) 13(报警限值) 14/16(状态文本)
               15(报警高限) 17(质量) 20(脉搏波采集中/暂停中) 21(监护时间)
               91(探头已接入) 19(波形) 1/6(暂停↔继续 两个叠放按钮的显隐)
       设置页  1/2(阈值) 10/11(进度条/滑块) 12(亮度%) 13(声音提示状态)
       趋势    12/14(历史曲线，UINT16/每点 2 字节) 13(记录时段)
               10/11(最近值) 15..18(时间刻度) 1/2/3(时段按钮选中态)
       报警记录 20..35(4 行文本) 10(条数) 11(空态)
       探头状态 13(探头状态) 16(质量进度条)
       暂停/清空弹窗 10(提示语)
       屏 RTC  用 EE 81 指令设置（控件 92 是 rtc 类型，写文本无效）

     画面跳转：
       button_type=0 的按钮用 switch 属性由屏原生跳页（btn_pause、取消/返回键）；
       需要 MCU 干活的按钮统一是 button_type=1 + button_style=0 + switch=""，
       它们只上报事件，跳页必须由 MCU 收到通知后发 EE B1 00 完成。 */

/* 暂停监护状态：由屏"暂停监护"确认按钮的通知翻转。
   暂停时数值显示冻结值（保留暂停前最后一次有效读数）、停止推波形、停止趋势采样、
   停止报警判定，监护时间冻结；恢复后随实测数据变化。 */
extern uint8_t g_paused;
#if defined(DEBUG_PPG) && DEBUG_PPG
extern volatile uint16_t g_pr_loss_streak;  /* 诊断：连续丢 PR 的 on_uno_frame 次数 */
extern volatile uint16_t g_uno_last_n;        /* 诊断：上次 on_uno_frame 取到的采样数 */
extern volatile int16_t  g_last_pr_write;     /* 诊断：上次实际写进 g_meas.pulse_rate 的值 */
extern volatile uint8_t  g_last_pr_ok;        /* 诊断：上次 HMI_WaveGetPRDetail 的 ok */
extern volatile int16_t  g_last_pr_est;       /* 诊断：上次 HMI_WaveGetPRDetail 的 est */
extern volatile uint8_t  g_last_pr_valid;     /* 诊断：上次 valid 标志 */
#endif /* DEBUG_PPG */

/* 应用层初始化：设置屏 RTC、初始化趋势时间长度、下发初始背光。启动时调用一次。 */
void app_hmi_init(void);

void on_uno_frame(void);    /* 收到一帧 UNO 数据：取走积压采样推波形点（暂停时丢弃积压） */
void task_hmi_value(void);  /* 数值/状态刷新：每 400ms（内含报警判定与报警记录） */
/* 趋势：历史曲线每 2s 追加一点（暂停或数据无效时跳过）。
 *   2s 必须与屏 趋势.tft 的 sample_cycle=2 一致；
 *   同时刷新趋势页"最近值"与曲线下方时间刻度。 */
void task_hmi_trend(void);
void task_clock(void);      /* 软 RTC：每秒推进，并周期性同步到屏 */

/* 屏在线探测重握手：把 MCU 侧保存的趋势历史副本"重发全部历史"到血氧屏趋势曲线。
 *   分片回放（时间门控）避免阻塞主循环；作为 screen_watch 的 resync 回调，
 *   返回 1=完成，0=进行中。 */
uint8_t spo2_resync(void);

/* 血氧侧软 RTC 读写（供诊断打印 / 血压页改时间后同步两屏使用）。
   get：读出当前年月日时分秒；set：直接改写并把新时间立刻写进血氧屏
   （入参越界时忽略；两屏共用一个 g_rtc_epoch_tick 基准）。 */
void spo2_rtc_get(uint16_t *y, uint8_t *mon, uint8_t *day,
                  uint8_t *hh, uint8_t *mi, uint8_t *ss);
void spo2_rtc_set(uint16_t y, uint8_t mon, uint8_t day,
                  uint8_t hh, uint8_t mi, uint8_t ss);

/* 把软 RTC 日历换算成 Unix epoch 秒（1970-01-01 00:00:00 UTC 起算），
 * 供 CANopen 对象 2100:01 上送"UTC epoch 秒"。不含 SPO2_RTC_OFFSET_SEC 屏显补偿，
 * 用 MCU 内部真实走时，与《MPM_统一数据字典_v0.1》2100:01 对齐。 */
uint32_t spo2_rtc_get_epoch(void);

#endif /* __APP_BO_H */
