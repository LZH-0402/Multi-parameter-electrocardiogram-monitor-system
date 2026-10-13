/******************************************************************************
* File Name          : hw_config.c
* Description        : 系统时钟与中断配置 (STM32F407ZGT6)
* [移植] STM32F103 -> STM32F407 修改点：
*   1) 删除 F1 手写 HSI/2*12=48MHz 的 PLL 流程。F4 的时钟由 system_stm32f4xx.c
*      中的 SystemInit() 在 startup 阶段配好：HSE 8MHz /8 *336 /2 = 168MHz。
*      Set_System() 改为同步 SystemCoreClock 变量 + 确认中断向量表。
*   2) 中断通道 USART1_IRQn -> USART2_IRQn（与串口屏实际接口一致），
*      并补齐原工程漏配的 SubPriority 与 IRQChannelCmd(ENABLE)。
*******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "hw_config.h"
#include "platform_config.h"
#include "system_stm32f4xx.h"      /* SystemCoreClockUpdate() / SystemCoreClock */


/*******************************************************************************
* Function Name  : Set_System
* Description    : 系统时钟确认与全局初始化
*******************************************************************************/
void Set_System(void)
{
    /* 启动文件已调用 SystemInit()：
         HSE=8MHz -> PLL_M=8 / PLL_N=336 / PLL_P=2 -> SYSCLK=168MHz
         HCLK=168MHz, PCLK1(APB1)=42MHz, PCLK2(APB2)=84MHz
       若晶振未起振，SystemInit() 内部会死等，程序不会执行到这里。 */

    SystemCoreClockUpdate();                 /* 同步全局变量 SystemCoreClock */

    SCB->VTOR = FLASH_BASE;                  /* 中断向量表位于 Flash 起始地址 */
}


/*******************************************************************************
* Function Name  : Interrupts_Config
* Description    : 配置串口屏使用的串口接收中断
*******************************************************************************/
void Interrupts_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);      /* 1位抢占 + 3位子优先级 */

    NVIC_InitStructure.NVIC_IRQChannel                   = HMI_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = HMI_USART_PREEMPT_PRIO;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = HMI_USART_SUB_PRIO;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/*************************** END OF FILE **************************************/
