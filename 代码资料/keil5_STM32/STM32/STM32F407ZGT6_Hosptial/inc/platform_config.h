/******************************************************************************
* File Name          : platform_config.h
* Description        : 板级硬件配置（STM32F407ZGT6 + 大彩串口屏）
*                      [移植] 原文件为 STM32F103 评估板 USB 配置，本工程不使用 USB，
*                             重写为「串口屏接口 + 中断优先级」配置。
* 说明：换串口/换引脚时，只改本文件即可，无需改动 hmi_user_uart.c / hw_config.c
*******************************************************************************/

#ifndef __PLATFORM_CONFIG_H
#define __PLATFORM_CONFIG_H

#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"   /* 提供 GPIO_AF_USART2 等外设宏(有头文件保护，重复包含安全) */

/*==============================================================================
 *  串口屏(HMI)通信接口配置
 *  当前：USART2，PA2=TX，PA3=RX，复用功能 AF7
 *
 *  波特率：必须与 VisualTFT 工程 SPO2_Hospital.tftprj 里的 DeviceBaudRate 对应。
 *    该工程当前 DeviceBaudRate="7"  ->  115200
 *    索引表：{1200,2400,4800,9600,19200,38400,57600,115200,1000000,2000000,
 *             218750,437500,875000,921800,2500000}
 *    即：3=9600，4=19200，7=115200
 *    传 PPG 波形需要带宽，115200 才够用；若屏幕仍烧着 9600 的固件，
 *    这里必须改回 19200 并同步调小波形帧频(WAVE_CHUNK/采样率)。
 *
 *  可选方案（把下面整组宏换成对应值即可）：
 *    USART1 : PA9 /PA10, GPIO_AF_USART1,  RCC_APB2Periph_USART1, USART1_IRQn,
 *             NVIC_IRQChannel = USART1_IRQn，中断函数名 USART1_IRQHandler
 *    USART3 : PB10/PB11, GPIO_AF_USART3,  RCC_APB1Periph_USART3, USART3_IRQn
 *    USART6 : PC6 /PC7 , GPIO_AF_USART6,  RCC_APB2Periph_USART6, USART6_IRQn
 *============================================================================*/
#define HMI_USART                   USART2
#define HMI_USART_CLK               RCC_APB1Periph_USART2
#define HMI_USART_IRQn              USART2_IRQn
#define HMI_USART_BAUDRATE          115200         /* 与 VisualTFT 工程 DeviceBaudRate=7 一致 */

/* TX / RX 引脚 */
#define HMI_GPIO_PORT               GPIOA
#define HMI_GPIO_CLK                RCC_AHB1Periph_GPIOA

#define HMI_TX_PIN                  GPIO_Pin_2
#define HMI_TX_PINSOURCE            GPIO_PinSource2
#define HMI_RX_PIN                  GPIO_Pin_3
#define HMI_RX_PINSOURCE            GPIO_PinSource3
#define HMI_GPIO_AF                 GPIO_AF_USART2

/* 中断优先级：CMSIS 优先级分组 Group_1 -> 抢占 0~1，子优先级 0~7 */
#define HMI_USART_PREEMPT_PRIO      1
#define HMI_USART_SUB_PRIO          1

/*==============================================================================
 *  系统时钟（由 system_stm32f4xx.c 的 SystemInit() 完成配置）
 *    HSE  = 8 MHz
 *    PLL  = HSE / PLL_M(8) * PLL_N(336) / PLL_P(2) = 168 MHz
 *    SYSCLK 168MHz / HCLK 168MHz / PCLK1 42MHz / PCLK2 84MHz
 *============================================================================*/
#define HSE_FREQ_HZ                 8000000UL
#define SYSCLK_FREQ_HZ              168000000UL

#endif /* __PLATFORM_CONFIG_H */

/*************************** END OF FILE **************************************/
