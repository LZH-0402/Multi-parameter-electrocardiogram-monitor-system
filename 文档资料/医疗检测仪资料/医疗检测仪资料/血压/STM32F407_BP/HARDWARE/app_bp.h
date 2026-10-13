/**
 * app_bp.h - HKB-08B+ 血压测量 → 大彩串口屏 (SPL 版整合，神舟号 STM32F407)
 */
#ifndef APP_BP_H
#define APP_BP_H

#include <stdint.h>
#include "stm32f4xx.h"

/* ---- 步骤5 开关：USART3(屏)/UART5(传感器) 的接收方式 ----
 *   1 = DMA + 空闲中断(IDLE)：线路空闲(一帧收完)才进一次中断，彻底防高速丢字节（步骤5 默认）。
 *   0 = 回退单字节 RXNE 中断（步骤4 行为）。
 *   stm32f4xx_it.c 也要用这个宏选 ISR 分支，所以放在头文件里。 */
#ifndef RX_USE_DMA_IDLE
#define RX_USE_DMA_IDLE 1
#endif

/* 应用初始化 / 主循环（在 main.c 调用） */
void app_init(void);
void app_loop(void);

/* 串口接收回调：步骤1仅回显到调试口；后续步骤在此接传感器/屏解析 */
void on_uart_rx(USART_TypeDef *u, uint8_t b);

/* 步骤5：DMA+空闲中断 收尾函数，由 USART3(屏)/UART5(传感器) 的 IDLE 中断调用——
 * 把 DMA 暂存区里已收的字节搬进环形缓冲，并重启 DMA。 */
void on_uart_rx_idle(USART_TypeDef *u);

/* 毫秒级 tick（SysTick 1ms 累加） */
uint32_t get_tick(void);
extern volatile uint32_t g_tick;

#endif /* APP_BP_H */
