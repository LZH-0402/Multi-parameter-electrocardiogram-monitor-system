#include "stm32f10x.h"
#include "LED.h"
#include "IIC.h"
#include "delay.h"
#include "usart.h"
#include "stdio.h"
/*
Keil: MDK5.10.0.2
MCU:stm32f103c8
硬件接法：
GY-614---STM32
SCL---PB6
SDA---PB7

软件说明:
	IIC时钟频率需低于400Khz；
	中断函数位于stm32f10x_it.c
联系方式：
http://shop62474960.taobao.com/?spm=a230r.7195193.1997079397.2.9qa3Ky&v=1
*/

int fputc(int ch, FILE *f)
{
 while (!(USART1->SR & USART_FLAG_TXE));
 USART_SendData(USART1, (unsigned char) ch);// USART1 可以换成 USART2 等
 return (ch);
}
static void NVIC_Configuration(void)
{
  NVIC_InitTypeDef NVIC_X;
  
  /* 4个抢占优先级，4个响应优先级 */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
  /*抢占优先级可打断中断级别低的中断*/
	/*响应优先级按等级执行*/
	NVIC_X.NVIC_IRQChannel = USART1_IRQn;//中断向量
  NVIC_X.NVIC_IRQChannelPreemptionPriority = 0;//抢占优先级
  NVIC_X.NVIC_IRQChannelSubPriority = 0;//响应优先级
  NVIC_X.NVIC_IRQChannelCmd = ENABLE;//使能中断响应
  NVIC_Init(&NVIC_X);
}

typedef struct
{
    float e;
    float to;
    float ta;
    float bo;
} gyir;
 void I2C_GPIO_Config(void);

int main(void)
{
	u8  raw_data[9]={0},data=0;
	u16 datas[4]={0},delay_t;

	gyir my_ir;

	u8 td=0;
	delay_init(72);

	LED_Int(GPIOB,GPIO_Pin_9,RCC_APB2Periph_GPIOB);
	NVIC_Configuration();
	Usart_Int(115200);
	I2C_GPIO_Config();
	
	delay_ms(300);//等待模块初始化完成
	td=2;
	 Single_WriteI2C_byte(0xa4,0x02,2);//设置更新频率5hz
       switch(td)
       {
         case 0:delay_t=1010;break;
         case 1:delay_t=510;break;
         case 2:delay_t=210;break;
         case 3:delay_t=110;break;
       }
        delay_ms(1); 
        Single_WriteI2C_byte(0xa4,0x07,95);//设置发射率0.95
			  delay_ms(1);
			  Single_WriteI2C_byte(0xa4,0x06,95);//设置温度补偿 95-100=-5度
        delay_ms(1); 
	while(1)
	{
		
			if(Single_ReadI2C(0xa4,0x07,raw_data,7))
			{
				my_ir.to=(raw_data[1]<<8)|raw_data[2];
				my_ir.ta=(raw_data[3]<<8)|raw_data[4];
				my_ir.bo=(raw_data[5]<<8)|raw_data[6];
				my_ir.e=raw_data[0];
				printf("E: %.2f,",(float)my_ir.e/100);
		    printf("  to: %.2f,",(float)my_ir.to/100);
			  printf("  ta: %.2f,",(float)my_ir.ta/100);
			  printf(" bo %.2f\r\n ",(float)my_ir.bo/100);  
			}
			 delay_ms(delay_t); //等待更新
	
		
		
	}
}
// funtion SCL_H,SCL_L,SDA_H,SDA_L;
// funtion1 SCL_read,SDA_read;
 //funtion2 sum;
 void I2C_GPIO_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
		/* 使能与 I2C有关的时钟 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE );  

	 /* PC3-I2C_SCL、PC5-I2C_SDA*/
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6| GPIO_Pin_7; 
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz; 
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD; 
	GPIO_Init(GPIOB, &GPIO_InitStructure); 
	GPIO_SetBits(GPIOB,GPIO_Pin_6|GPIO_Pin_7);

}