/**
 * app_bp.h - HKB-08B+ 血压测量 → 大彩串口屏 (SPL 版整合，神舟号 STM32F407)
 */
#ifndef APP_BP_H
#define APP_BP_H

#include <stdint.h>
#include "stm32f4xx.h"
#include "merge_cfg.h"

/* ---- BP 子系统（血压屏 USART3 + 血压传感器 UART5）的接收方式 ----
 *   1 = DMA + 空闲中断(IDLE)（原始单屏工程做法）。
 *   0 = 单字节 RXNE 中断（当前双屏并行方案采用）。
 * 双屏并行后，BP 的屏口在 USART3(9600)、传感器口在 UART5(115200)，两路数据量都很小
 * （屏只在按键时回几字节；传感器约 10Hz×几字节），RXNE 逐字节收完全够用，且不需要
 * 再申请 DMA 流、避免与其它外设抢流，故统一用 RXNE=0。
 * stm32f4xx_it.c 也用这个宏选 ISR 分支，所以放在头文件里。 */
#ifndef RX_USE_DMA_IDLE
#define RX_USE_DMA_IDLE 0
#endif

/* 应用初始化 / 主循环（在 main.c 调用） */
void app_init(void);
void app_loop(void);

/* 串口接收回调：步骤1仅回显到调试口；后续步骤在此接传感器/屏解析 */
void on_uart_rx(USART_TypeDef *u, uint8_t b);

/* DMA+空闲中断 收尾函数（仅在 RX_USE_DMA_IDLE=1 时编译/使用）——
 * 把 DMA 暂存区里已收的字节搬进环形缓冲，并重启 DMA。
 * 当前 RX_USE_DMA_IDLE=0（走 RXNE），此函数不会被调用。 */
void on_uart_rx_idle(USART_TypeDef *u);

/* 毫秒级 tick（SysTick 1ms 累加，统一复用 SPO2 的 uwTick） */
uint32_t get_tick(void);
/* 合并工程：g_tick 直接别名到 SPO2 sys.c 中的 uwTick，避免双 SysTick 计数 */
extern volatile uint32_t uwTick;
#define g_tick uwTick

#endif /* APP_BP_H */
