/******************************************************************************
* File Name          : stm32f4xx_it.c
* Description        : STM32F407 中断服务函数
*                      [移植] 由 STM32F103(stm32f10x_it.c) 移植而来
*                      修改点：USART1 -> USART2，与 UartInit()/SendChar() 统一
*******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "hw_config.h"
#include "cmd_queue.h"
#include "platform_config.h"

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    /* Go to infinite loop when Hard Fault exception occurs */
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/******************************************************************************/
/*            STM32F4xx Peripherals Interrupt Handlers                        */
/******************************************************************************/

/*******************************************************************************
* Function Name  : USART2_IRQHandler
* Description    : 串口屏数据接收中断，收到一字节即压入指令队列
*******************************************************************************/
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(HMI_USART, USART_IT_RXNE) != RESET)
    {
        uint8_t data = USART_ReceiveData(HMI_USART);
        queue_push(data);
    }
}

extern volatile uint32_t timer_tick_count;

/*******************************************************************************
* Function Name  : SysTick_Handler
* Description    : 10ms 节拍，timer_tick_count + 1
*******************************************************************************/
void SysTick_Handler(void)
{
    ++timer_tick_count;
}

/*************************** END OF FILE **************************************/
