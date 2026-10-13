/**
  ******************************************************************************
  * @file    Project/STM32F4xx_StdPeriph_Templates/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @version V1.4.0
  * @date    04-August-2014
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2014 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "app_bp.h"   /* on_uart_rx / g_tick */


/** @addtogroup Template_Project
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
extern volatile uint32_t g_tick;   /* 由 app_bp.c 定义，SysTick 累加 */
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  g_tick++;   /* 1ms 计时，app_bp.c 的 get_tick()/delay_ms() 依赖它 */
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/**
  * @brief  USART1 中断（PC 调试口，步骤1仅回显）
  */
void USART1_IRQHandler(void)
{
  if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
    uint8_t b = USART_ReceiveData(USART1);
    on_uart_rx(USART1, b);
  }
}

/**
  * @brief  USART3 中断（屏 RS232，PB10/PB11 9600）
  *         步骤5：RX 走 DMA + 空闲中断(IDLE)。收到一帧(线路空闲)才进中断。
  */
void USART3_IRQHandler(void)
{
#if RX_USE_DMA_IDLE
  if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET) {
    volatile uint32_t tmp;
    tmp = USART3->SR; tmp = USART3->DR; (void)tmp;   /* 读 SR 再读 DR，清 IDLE 标志 */
    on_uart_rx_idle(USART3);                          /* 搬字节进环形缓冲 + 重启 DMA */
  }
#else
  if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET) {
    uint8_t b = USART_ReceiveData(USART3);
    on_uart_rx(USART3, b);
  }
#endif
}

/**
  * @brief  UART5 中断（传感器 TTL，PC12/PD2 115200）
  *         步骤5：RX 走 DMA + 空闲中断(IDLE)。
  */
void UART5_IRQHandler(void)
{
#if RX_USE_DMA_IDLE
  if (USART_GetITStatus(UART5, USART_IT_IDLE) != RESET) {
    volatile uint32_t tmp;
    tmp = UART5->SR; tmp = UART5->DR; (void)tmp;   /* 清 IDLE */
    on_uart_rx_idle(UART5);
  }
#else
  if (USART_GetITStatus(UART5, USART_IT_RXNE) != RESET) {
    uint8_t b = USART_ReceiveData(UART5);
    on_uart_rx(UART5, b);
  }
#endif
}

/**
  * @}
  */ 


/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
