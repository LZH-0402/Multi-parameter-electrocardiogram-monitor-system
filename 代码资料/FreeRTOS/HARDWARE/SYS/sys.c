#include "stm32f4xx.h"
#include "sys.h"

/* 由 stm32f4xx_it.c 的 SysTick_Handler() 每秒累加 1000 次 */
volatile uint32_t uwTick = 0;

uint32_t millis(void)
{
    return uwTick;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = uwTick;
    while ((uwTick - start) < ms)
    {
        /* 空转等待；SysTick 中断会在后台累加 uwTick */
    }
}

void SysTick_Init(void)
{
    /* SystemCoreClock 已在 system_stm32f4xx.c 中设为 168000000。
       CMSIS 的 SysTick_Config 以 HCLK 为时钟源，计数 168000 次 = 1ms。 */
    if (SysTick_Config(SystemCoreClock / 1000) != 0)
    {
        /* 配置失败理论上不会发生，卡死便于发现硬件/时钟问题 */
        while (1)
        {
        }
    }
}
