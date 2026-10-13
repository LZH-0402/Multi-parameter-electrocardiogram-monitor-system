/******************************************************************************
* File Name          : stm32f4xx_it.h
* Description        : STM32F407 中断服务函数声明
*                      [移植] 由 STM32F103(stm32f10x_it.h) 移植而来
*******************************************************************************/

#ifndef __STM32F4xx_IT_H
#define __STM32F4xx_IT_H

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx.h"

/* Exported functions ------------------------------------------------------- */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

/* 串口屏通信串口中断，具体是哪个串口见 platform_config.h */
void USART2_IRQHandler(void);

#endif /* __STM32F4xx_IT_H */

/*************************** END OF FILE **************************************/
