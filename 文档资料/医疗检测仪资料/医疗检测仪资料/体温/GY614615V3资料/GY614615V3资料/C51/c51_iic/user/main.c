#include <reg52.h>
#include "usart.h"
#include "iic.h"  
 #include "stdio.h"
/*
硬件接法：
GY-33----C51
SCL---P3^6
SDA---P3^7
C51---FT232
TX ---RX
RX ---TX
软件说明：

注：
	中断函数位于stc_it.c
联系方式：
http://shop62474960.taobao.com/?spm=a230r.7195193.1997079397.2.9qa3Ky&v=1
*/
char putchar (char c)   
{        
    ES=0;        
    SBUF = c;        
    while(TI==0);        
    TI=0;        
    ES=1;        
    return 0;
}
typedef struct
{
    float e;
    float to;
    float ta;
    float bo;
} gyir;
void delay_ms(unsigned int x)
{
	while(x--);
}
int main(void)
{
	u8  raw_data[9]={0};
	uint16_t datas[4]={0},delay_t;

	gyir my_ir;

	u8 td=0;

	Usart_Int(9600);
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
				datas[0]=my_ir.to=(raw_data[1]<<8)|raw_data[2];
				datas[1]=my_ir.ta=(raw_data[3]<<8)|raw_data[4];
				datas[2]=my_ir.bo=(raw_data[5]<<8)|raw_data[6];
				datas[3]=my_ir.e=raw_data[0];
				printf("E: %.2f,",(float)my_ir.e/100);
		    printf("  to: %.2f,",(float)my_ir.to/100);
			  printf("  ta: %.2f,",(float)my_ir.ta/100);
			  printf(" bo %.2f\r\n ",(float)my_ir.bo/100);  
			}
			 delay_ms(delay_t); //等待更新
	
		
		
	}
}
