/******************************************************************************
* File Name          : stm32f4xx_conf.h
* Description        : STM32F4xx 标准外设库配置文件
*                      [移植] 由 stm32f10x_conf.h 改写，仅保留本工程用到的外设
* 说明：定义了 USE_STDPERIPH_DRIVER 后，stm32f4xx.h 会自动包含本文件
*******************************************************************************/

#ifndef __STM32F4xx_CONF_H
#define __STM32F4xx_CONF_H

/* 本工程实际用到的外设驱动 */
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_flash.h"
#include "stm32f4xx_exti.h"
#include "stm32f4xx_syscfg.h"
#include "misc.h"          /* NVIC 与 SysTick 高层函数 */

/* 如需其它外设，取消对应行的注释即可 */
/* #include "stm32f4xx_spi.h"   */
/* #include "stm32f4xx_dma.h"   */
/* #include "stm32f4xx_tim.h"   */
/* #include "stm32f4xx_adc.h"   */
/* #include "stm32f4xx_i2c.h"   */

#ifdef USE_FULL_ASSERT

#define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))

void assert_failed(uint8_t* file, uint32_t line);

#else
#define assert_param(expr) ((void)0)
#endif /* USE_FULL_ASSERT */

#endif /* __STM32F4xx_CONF_H */

/*************************** END OF FILE **************************************/
