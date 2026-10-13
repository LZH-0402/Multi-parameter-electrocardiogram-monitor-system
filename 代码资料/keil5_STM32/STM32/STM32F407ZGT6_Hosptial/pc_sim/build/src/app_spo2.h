/*!
*  \file app_spo2.h
*  \brief 血氧监护应用层（大彩串口屏 SPO2_Hospital 工程）
*
*  职责：把测量数据(g_meas)映射到 7 个画面的控件上，并处理屏上传的按键/输入通知。
*  协议字节全部由 hmi_driver.c 负责，本层只调用 SetXxx() / BatchXxx() 等 API。
*/
#ifndef __APP_SPO2_H
#define __APP_SPO2_H

#include "hmi_user_uart.h"

/*! 应用初始化：应在 UartInit() 与 300ms 上电延时之后调用一次 */
void App_Init(void);

/*! 主循环节拍：在 while(1) 里不限速地反复调用（内部按 10ms 自行分频） */
void App_Tick(void);

/*! 屏握手完成 / 需要重新下发全部初值时的入口（可重复调用） */
void App_PushAll(void);

/*! 当前画面 ID（0~6），与 hmi_ids.h 的 SCR_xxx 对应 */
uint16 App_CurrentScreen(void);

/*! 监护是否处于运行状态：1 运行 / 0 已暂停 */
uint8 App_IsRunning(void);

#endif /* __APP_SPO2_H */
