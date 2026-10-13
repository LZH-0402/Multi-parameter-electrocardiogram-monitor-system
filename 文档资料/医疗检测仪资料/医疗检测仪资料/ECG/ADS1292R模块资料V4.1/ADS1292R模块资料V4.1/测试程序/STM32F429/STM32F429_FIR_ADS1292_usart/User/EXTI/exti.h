//-----------------------------------------------------------------
// 程序描述:
// 		 外部中断驱动程序头文件
// 作    者: 凌智电子
// 开始日期: 2018-08-04
// 完成日期: 2018-08-04
// 修改日期: 
// 当前版本: V1.0
// 历史版本:
//  - V1.0: (2018-08-04)外部中断初始化，中断时执行相应的事情
// 调试工具: 凌智STM32F429+Cyclone IV电子系统设计开发板、LZE_ST_LINK2
// 说    明: 
//
//-----------------------------------------------------------------
#ifndef _EXTI_H
#define _EXTI_H
#include "stm32f429_winner.h"
//-----------------------------------------------------------------
// EXTI引脚定义
//-----------------------------------------------------------------
#define KEY2_EXTI_PIN                                GPIO_PIN_4
#define KEY2_EXTI_GPIO_PORT                          GPIOB
#define KEY2_EXTI_GPIO_CLK_ENABLE()                  __HAL_RCC_GPIOB_CLK_ENABLE()  
#define KEY2_EXTI_GPIO_CLK_DISABLE()                 __HAL_RCC_GPIOB_CLK_DISABLE() 
#define KEY2_EXTI_IRQn                          		 EXTI4_IRQn

#define KEY3_EXTI_PIN                                GPIO_PIN_6
#define KEY3_EXTI_GPIO_PORT                          GPIOB
#define KEY3_EXTI_GPIO_CLK_ENABLE()                  __HAL_RCC_GPIOB_CLK_ENABLE()  
#define KEY3_EXTI_GPIO_CLK_DISABLE()                 __HAL_RCC_GPIOB_CLK_DISABLE() 
#define KEY3_EXTI_IRQn                          		 EXTI9_5_IRQn

#define KEY4_EXTI_PIN                                GPIO_PIN_9
#define KEY4_EXTI_GPIO_PORT                          GPIOB
#define KEY4_EXTI_GPIO_CLK_ENABLE()                  __HAL_RCC_GPIOB_CLK_ENABLE()  
#define KEY4_EXTI_GPIO_CLK_DISABLE()                 __HAL_RCC_GPIOB_CLK_DISABLE() 
#define KEY4_EXTI_IRQn                          		 EXTI9_5_IRQn

//-----------------------------------------------------------------
// 外部函数声明
//-----------------------------------------------------------------
extern void EXTI_Init(void);
//-----------------------------------------------------------------

#endif
//-----------------------------------------------------------------
// End Of File
//-----------------------------------------------------------------
