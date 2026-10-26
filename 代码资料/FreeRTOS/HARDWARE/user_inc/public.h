#ifndef __PUBLIC_H
#define __PUBLIC_H

#include "stm32f4xx.h"
#include "led.h"
/* SysTick 由 FreeRTOS 内核接管（1ms 节拍，configTICK_RATE_HZ=1000）；
   uwTick/millis() 由 HARDWARE/SYS/sys.c 提供，并由 vApplicationTickHook 递增。
   因此不再包含示例的 SysTick.h。 */

#endif
