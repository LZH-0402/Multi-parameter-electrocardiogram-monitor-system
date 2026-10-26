#include "led.h"
#include "stm32f4xx.h"


/**
  * @brief  LED初始化函数
  * @note   初始化两个LED灯，引脚分别为PF9和PF10
  * @param  void 
  * @retval None
  */
void LED_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);//通电还是断电
	
	//开始配置F端口下的9号跟10号引脚。
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_9|GPIO_Pin_10; 
	GPIO_InitStructure.GPIO_Mode= GPIO_Mode_OUT; //输出模式
	GPIO_InitStructure.GPIO_OType=GPIO_OType_PP; //推挽输出
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_UP; //上拉模式，暂时没讲
	GPIO_InitStructure.GPIO_Speed=GPIO_High_Speed; //引脚响应速度
	
	GPIO_Init(GPIOF, &GPIO_InitStructure);
}
