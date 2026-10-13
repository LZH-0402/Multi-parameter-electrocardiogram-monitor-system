/******************************************************************************
**                             广州大彩光电科技有限公司
**                             http://www.gz-dc.com
**-----------------------------------文件信息--------------------------------------------
** 文件名称:   ulitity.c
** 文件说明:   延时与系统节拍
** [移植] STM32F103(48MHz) -> STM32F407(168MHz)
**   SysTick 源为 HCLK/8：F1 为 48M/8=6MHz，F4 为 168M/8=21MHz，
**   因此重装载值必须由写死的 480000/100 改为 SystemCoreClock/8/100（=210000，10ms）
--------------------------------------------------------------------------------------*/
#include "ulitity.h"
#include "system_stm32f4xx.h"

extern volatile  uint32 timer_tick_count;


/*!
* 名    称： delay_ms
* 功    能： 延时n毫秒（依赖 10ms 节拍，精度 ±10ms）
* 入口参数： n-延时时间
* 出口参数： 无
*/
void delay_ms(uint32 delay)
{
    uint32 tick = timer_tick_count;
    while(1)
    {
        if(timer_tick_count-tick>delay/10)
            break;
    }
}

/*!
*名    称： systicket_init
*功    能 ：节拍定时器 10ms
*入口参数： 无
*出口参数： 无
*/
void systicket_init(void)
{
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);   /* SysTick 时钟 = 168M/8 = 21MHz */
    SysTick_Config(SystemCoreClock/8/100);                  /* 21MHz/100 = 10ms 进入一次中断 */
}

/*************************** END OF FILE **************************************/
