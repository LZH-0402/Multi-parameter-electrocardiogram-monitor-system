#ifndef __APP_BO_H
#define __APP_BO_H

#include <stdint.h>

/* 应用层：把 UNO 解析出的测量数据搬到屏幕上。
   裸机：用 SysTick 提供的 millis() 驱动；RTOS：把各节拍各自做成任务即可。

   ★★ 控件归属（2026-10-10 二次更正，以此为准）★★
   屏工程自带的 main.lua **会执行**，并且会和 MCU 抢着写同一批控件
   （它自己也维护一份 paused / monitor_sec，与 MCU 的 g_paused / s_monitor_sec
   互不同步，这正是"暂停后文案不变、监护时间永远是 00:00:01"的根因）。
   现已把 main.lua 换成空壳脚本（原版备份 main.lua.原版备份.txt），
   于是 **全部动态内容都由 MCU 独占写入**：

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
   暂停时停止刷新数值/状态、停止推波形、停止趋势采样，监护时间冻结。 */
extern uint8_t g_paused;

/* 应用层初始化：设置屏 RTC、初始化趋势时间长度、下发初始背光。启动时调用一次。 */
void app_hmi_init(void);

void on_uno_frame(void);    /* 收到一帧 UNO 数据：推一个 PPG 波形点（暂停时忽略） */
void task_hmi_value(void);  /* 数值/状态刷新：每 400ms（内含报警判定与报警记录） */
/* 趋势：历史曲线每 2s 追加一点（暂停时跳过）。
 *   2s 必须与屏 趋势.tft 的 sample_cycle=2 一致；
 *   同时刷新趋势页"最近值"与曲线下方时间刻度。 */
void task_hmi_trend(void);
void task_clock(void);      /* 软 RTC：每秒推进，并周期性同步到屏 */

/* 血氧侧软 RTC 读写（供诊断打印 / 血压页改时间后同步两屏使用）。
   get：读出当前年月日时分秒；set：直接改写并把新时间立刻写进血氧屏。 */
void spo2_rtc_get(uint16_t *y, uint8_t *mon, uint8_t *day,
                  uint8_t *hh, uint8_t *mi, uint8_t *ss);
void spo2_rtc_set(uint16_t y, uint8_t mon, uint8_t day,
                  uint8_t hh, uint8_t mi, uint8_t ss);

#endif /* __APP_BO_H */
