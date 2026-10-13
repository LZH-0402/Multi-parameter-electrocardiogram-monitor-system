/*!
*  \file dbg_log.h
*  \brief PC 调试日志（USART1，PA9=TX / PA10=RX，115200 8N1）
*
*  对应《STM32F407_AFE4490_M系列串口屏_快速上手开发规划》：
*    F-01 上电自检结果可见、F-03 带序号样本输出、7.3 task_log 不阻塞采集。
*
*  设计要点：
*    - 发送走 TX 环形缓冲 + TXE 中断，Dbg_Log() 只压缓冲不等待，不阻塞采集；
*    - 缓冲满时丢弃并计数（tx_drop），状态行里上报；
*    - 原始样本从 meas.c 的环形缓冲弹出打印，采样与日志彻底解耦。
*/
#ifndef __DBG_LOG_H
#define __DBG_LOG_H

#include "hmi_user_uart.h"      /* uint8 / uint16 / uint32 / int16 / int32 */

/*! 原始样本逐点打印开关：1 = 每采样点打一行 [S]（F-03 验收用），
*  0 = 只保留每秒状态行 [ST]。115200 下 100Hz 样本约 4.8KB/s，可持续。 */
#define DBG_LOG_RAW_EN      1

/*! 初始化 USART1 日志口（上电调用一次，115200 8N1） */
void DbgLog_Init(void);

/*! 格式化输出一行日志（自动追加 \r\n）。只压 TX 缓冲，立即返回。 */
void Dbg_Log(const char *fmt, ...);

/*! 日志任务：主循环不限速调用。
*  每次最多弹出若干原始样本打印（DBG_LOG_RAW_EN=1 时），
*  并每 1s 输出一行状态（数值/探头/有效性/故障码/丢样计数）。 */
void DbgLog_Task(void);

/*! TX 缓冲累计丢弃字节数（正常应恒为 0） */
uint32 DbgLog_TxDrops(void);

#endif /* __DBG_LOG_H */
