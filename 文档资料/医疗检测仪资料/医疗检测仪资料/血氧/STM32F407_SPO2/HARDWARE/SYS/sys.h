#ifndef __SYS_H
#define __SYS_H

#include <stdint.h>

/* 系统节拍：基于 SysTick（HCLK=168MHz，1ms 中断一次） */
extern volatile uint32_t uwTick;  /* 由 SysTick_Handler() 每 1ms 累加，定义在 sys.c */
void SysTick_Init(void);     /* 配置并启动 1ms SysTick */
uint32_t millis(void);       /* 上电以来的毫秒数（替代 HAL_GetTick） */
void delay_ms(uint32_t ms);  /* 阻塞延时（非精准，仅供简单等待） */

#endif /* __SYS_H */
