/**
  ******************************************************************************
  * @file    FreeRTOS / USER / stm32f4xx_it.c
  * @brief   中断服务例程：双屏双传感器并行（血氧 SPO2 + 血压 BP）
  *
  * 中断分配（与 STM32F407_HOSPITAL 一致）：
  *   USART1(PA9/10) 调试口 115200（RXNE 未使能，仅发送；此处保留回显分支）
  *   USART2(PA2/3)  血氧屏 SPO2 -> RXNE -> HMI_RxByte(dc_hmi)
  *   USART3(PB10/11) 血压屏 BP   -> RXNE -> on_uart_rx(USART3) -> s_q2
  *   UART5 (PC12/PD2) 血压传感器 HKB-08B+ -> RXNE -> on_uart_rx(UART5) -> s_q3
  *   USART6(PC6/7)  血氧传感器 UNO(AFE4490) -> RXNE -> UNO_ParseByte
  *                       （USART6_IRQHandler 定义在 HARDWARE/USART/usart.c，此处不重复）
  *
  * 注意：SysTick / SVC / PendSV 三个异常**不要**在此定义 —— 它们由 FreeRTOS
  *       移植层（portasm.s / port.c）提供，且 FreeRTOSConfig.h 已把
  *       vPortSVCHandler->SVC_Handler、xPortPendSVHandler->PendSV_Handler、
  *       xPortSysTickHandler->SysTick_Handler 做了映射。重复定义会导致链接冲突
  *       或破坏内核最低优先级保证。uwTick 的递增改由 vApplicationTickHook() 完成。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "app_bp.h"       /* on_uart_rx */
#include "dc_hmi.h"       /* HMI_RxByte */
#include "merge_cfg.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
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

void DebugMon_Handler(void)
{
}

/* SVC_Handler / PendSV_Handler / SysTick_Handler 由 FreeRTOS 移植层提供，
   本文件不定义（见文件头说明）。 */

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/******************************************************************************/

/**
  * @brief  USART1 中断（PC 调试口，115200）。
  *         BP 的回显路径：把收到的字节经 on_uart_rx 处理（非屏/非传感器时原样回显）。
  *         注意：DEBUG_UART_Init() 未使能 RXNE 中断，此分支实际不会触发。
  */
void USART1_IRQHandler(void)
{
  if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
    uint8_t b = USART_ReceiveData(USART1);
    on_uart_rx(USART1, b);
  }
}

/**
  * @brief  USART2 中断（血氧屏 SPO2，USART2(PA2/PA3)）。
  *         RXNE -> HMI_RxByte（dc_hmi 帧收集）。
  */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(USART2);
        HMI_RxByte(b);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

/**
  * @brief  USART3 中断（血压屏 BP，USART3(PB10/11)）。
  *         屏只在按键时回几字节，RXNE -> on_uart_rx(USART3) -> 屏环形缓冲 s_q2。
  */
void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(USART3);
        on_uart_rx(USART3, b);
    }
}

/**
  * @brief  UART5 中断（血压传感器 HKB-08B+，UART5(PC12/PD2)）。
  *         RXNE -> on_uart_rx(UART5) -> 传感器环形缓冲 s_q3。
  */
void UART5_IRQHandler(void)
{
    if (USART_GetITStatus(UART5, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(UART5);
        on_uart_rx(UART5, b);
    }
}

/* USART6_IRQHandler（UNO 血氧传感器）定义见 HARDWARE/USART/usart.c，此处不重复定义 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
