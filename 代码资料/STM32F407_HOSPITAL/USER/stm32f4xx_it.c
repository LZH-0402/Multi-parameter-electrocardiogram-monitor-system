/**
  ******************************************************************************
  * @file    STM32F407_HOSPITAL / USER / stm32f4xx_it.c
  * @brief   合并工程中断服务例程：血氧(SPO2) + 血压(BP) 两套子系统共用
  *
 * 中断分配（双屏双传感器并行，见《合并过程记录.md》）：
 *   SysTick      -> uwTick++（1ms 节拍，BP 的 g_tick 经 #define 指向它）
 *   USART1(PA9/10) 调试口 115200（RXNE 未使能，仅发送）
 *   USART2(PA2/3)  血氧屏 SPO2  -> RXNE -> HMI_RxByte(dc_hmi)
 *   USART3(PB10/11) 血压屏 BP   -> RXNE -> on_uart_rx(USART3) -> s_q2（hmi_dacai 上行）
 *   UART5 (PC12/PD2) 血压传感器 HKB-08B+ -> RXNE -> on_uart_rx(UART5) -> s_q3
 *   USART6(PC6/7)  血氧传感器 UNO(AFE4490) -> RXNE -> UNO_ParseByte
 *                       （USART6_IRQHandler 定义见 HARDWARE/USART/usart.c，此处不重复）
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "sys.h"          /* uwTick */
#include "app_bp.h"       /* on_uart_rx / on_uart_rx_idle / (g_tick->uwTick 别名) */
#include "dc_hmi.h"       /* HMI_RxByte */
#include "uno.h"          /* UNO_ParseByte */
#include "merge_cfg.h"
#include "canopen_app.h"  /* canopen_app_CAN_interrupt */

/** @addtogroup Template_Project
  * @{
  */

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

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/**
  * @brief  SysTick Handler —— 唯一的 1ms 节拍源。
  *         uwTick 在 sys.c 中定义；BP 的 g_tick 经 app_bp.h 的
  *         #define g_tick uwTick 指向同一变量，两套子系统共用一个计数。
  */
void SysTick_Handler(void)
{
    uwTick++;
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/******************************************************************************/

/**
  * @brief  USART1 中断（PC 调试口，115200）。
  *         BP 的回显路径：把收到的字节经 on_uart_rx 处理（非屏/非传感器时原样回显）。
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

/**
  * @brief  CAN1 接收 FIFO0 中断。统一转调 CO_CANinterrupt（它内部一并处理 TX/SCE）。
  */
void CAN1_RX0_IRQHandler(void)
{
    canopen_app_CAN_interrupt();
}

/**
  * @brief  CAN1 发送完成中断。统一转调 CO_CANinterrupt。
  */
void CAN1_TX_IRQHandler(void)
{
    canopen_app_CAN_interrupt();
}

/**
  * @brief  CAN1 状态改变 / 错误中断(SCE)。统一转调 CO_CANinterrupt。
  */
void CAN1_SCE_IRQHandler(void)
{
    canopen_app_CAN_interrupt();
}

/* USART6_IRQHandler（UNO 血氧传感器）定义见 HARDWARE/USART/usart.c，此处不重复定义 */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
