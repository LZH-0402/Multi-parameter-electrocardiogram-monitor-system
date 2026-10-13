#ifndef __USART_H
#define __USART_H

#include <stdint.h>

/* 屏：USART2，PA2(TX) / PA3(RX)，115200 */
void USART2_Screen_Init(uint32_t baud);

/* UNO：USART6，PC6(TX,->UNO D0 暂不用) / PC7(RX,<-UNO D1)，115200。
   无需电平转换：UNO TX 为 5V，PC7 是 FT 脚可直连；STM32 TX 3.3V，UNO V_IH≈3.0V 可直连。 */
void USART6_UNO_Init(uint32_t baud);

#endif /* __USART_H */
