/*! 
*  \file hmi_user_uart.h
*  \brief 串口初始化与字节发送
*  \version 1.0(F407)
*  \date 2012-2018
*  \copyright 广州大彩光电科技有限公司
*/
#ifndef _USER_UART__
#define _USER_UART__

#include "stm32f4xx_it.h"     //根据用户MCU进行修改 [移植] stm32f10x_it.h -> stm32f4xx_it.h

#define uchar    unsigned char
#define uint8    unsigned char
#define uint16   unsigned short int
#define uint32   unsigned long
#define int16    short int
#define int32    long

/****************************************************************************
* 名    称： UartInit()
* 功    能： 串口初始化
* 入口参数： BaudRate 波特率
* 出口参数： 无
****************************************************************************/
void UartInit(uint32 BaudRate);

/****************************************************************************
* 名    称： SendChar()
* 功    能： 发送1个字节
* 入口参数： t  发送的字节
* 出口参数： 无
****************************************************************************/
void  SendChar(uchar t);

#endif
