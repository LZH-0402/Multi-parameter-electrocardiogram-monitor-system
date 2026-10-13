//-----------------------------------------------------------------
// 程序描述:
//    ADS1292心电实验
// 作    者: 凌智电子
// 开始日期: 2020-09-25
// 完成日期: 2020-09-25
// 修改日期: 2020-09-25
// 版    本: V1.0
//           ADS1292心电实验
// 调试工具: 凌智STM32核心开发板、LZE_ST LINK2、2.8寸液晶、ADS1292模块
// 说    明: 接口说明
//						ADS1292模块            核心板
// 						  PW(PWDN)		->  		 	PA0
// 					    ST(START)  	->   			PA1
// 						  CS  	 			->   			PA2
// 						  DR(DRDY)		->   			PA3
// 						  SCL(SCLK)		->   			PA5
// 						  OUT(DOUT)		->   			PA6
// 						  IN(DIN)			->   			PA7
//              5V 					->   			5V
//						  GND					->	 			GND
//
//					   三导联线					 心电模拟器
//						 红线（RL）		-> 				 RL
//						 黄线（LA）		-> 				 LA
//						 绿线（RA）		-> 				 RA
//
//					   三导联线						 		人体（导联线需要用电极片）
//						 红线（RL）		-> 				右脚
//						 黄线（LA）		-> 				左脚
//						 绿线（RA）		-> 				右手
//
//-----------------------------------------------------------------

//-----------------------------------------------------------------
// 头文件包含
//-----------------------------------------------------------------
#include "stm32f10x.h"
#include "Delay.h"
#include "LED.h"
#include "lze_lcd.h"
#include "usart.h"	
#include "ADS1292.h"	
#include "spi.h"
#include "EXTInterrupt.h"
#include "PeripheralInit.h"
#include "arm_math.h"

//-----------------------------------------------------------------
// 主程序
//-----------------------------------------------------------------
#define Draw_Number  		320    										// 绘制点数
#define Samples_Number  30    										// 采样点数
#define Block_Size      30     										// 调用一次arm_fir_f32处理的采样点个数
#define NumTaps        	129     									// 滤波器系数个数

uint32_t blockSize = Block_Size;									// 调用一次arm_fir_f32处理的采样点个数
uint32_t numBlocks = Samples_Number/Block_Size;   // 需要调用arm_fir_f32的次数

float32_t Input_data[Samples_Number]; 						// 输入缓冲区
float32_t Output_data[Samples_Number];         		// 输出缓冲区
float32_t firState[Block_Size + NumTaps - 1]; 		// 状态缓存，大小numTaps + blockSize - 1

u16 i;
u32 ch2_data;										// 通道二的数据
u8 flog;												// 触发中断标志位
u16 point_cnt;									// 两个峰值之间的采集点数，用于计算心率
u32 Cache_data[30];							// ADS1292采集数据缓存
u32 Draw_data[Draw_Number];			// 绘制波形数据

// 带通滤波器系数：采样频率为250Hz，截止频率为5Hz~40Hz 通过filterDesigner获取
const float32_t BPF_5Hz_40Hz[NumTaps]  = {
  3.523997657e-05,0.0002562592272,0.0005757701583,0.0008397826459, 0.000908970891,
  0.0007304374012,0.0003793779761,4.222582356e-05,-6.521392788e-05,0.0001839015895,
  0.0007320778677, 0.001328663086, 0.001635892317, 0.001413777587,0.0006883906899,
  -0.0002056905651,-0.0007648666506,-0.0005919140531,0.0003351111372, 0.001569915912,
   0.002375603188, 0.002117323689,0.0006689901347,-0.001414557919,-0.003109993879,
  -0.003462586319, -0.00217742566,8.629632794e-05, 0.001947802957, 0.002011778764,
  -0.0002987752669,-0.004264956806, -0.00809297245,-0.009811084718,-0.008411717601,
  -0.004596390296,-0.0006214127061,0.0007985962438,-0.001978532877,-0.008395017125,
   -0.01568987407, -0.02018531598, -0.01929843985, -0.01321159769,-0.005181713495,
  -0.0001112028476,-0.001950757345, -0.01125541423,  -0.0243169684, -0.03460548073,
   -0.03605531529, -0.02662901953, -0.01020727865, 0.004513713531, 0.008002913557,
  -0.004921500571, -0.03125274926, -0.05950148031, -0.07363011688, -0.05986980721,
   -0.01351031102,  0.05752891302,   0.1343045086,   0.1933406889,   0.2154731899,
     0.1933406889,   0.1343045086,  0.05752891302, -0.01351031102, -0.05986980721,
   -0.07363011688, -0.05950148031, -0.03125274926,-0.004921500571, 0.008002913557,
   0.004513713531, -0.01020727865, -0.02662901953, -0.03605531529, -0.03460548073,
    -0.0243169684, -0.01125541423,-0.001950757345,-0.0001112028476,-0.005181713495,
   -0.01321159769, -0.01929843985, -0.02018531598, -0.01568987407,-0.008395017125,
  -0.001978532877,0.0007985962438,-0.0006214127061,-0.004596390296,-0.008411717601,
  -0.009811084718, -0.00809297245,-0.004264956806,-0.0002987752669, 0.002011778764,
   0.001947802957,8.629632794e-05, -0.00217742566,-0.003462586319,-0.003109993879,
  -0.001414557919,0.0006689901347, 0.002117323689, 0.002375603188, 0.001569915912,
  0.0003351111372,-0.0005919140531,-0.0007648666506,-0.0002056905651,0.0006883906899,
   0.001413777587, 0.001635892317, 0.001328663086,0.0007320778677,0.0001839015895,
  -6.521392788e-05,4.222582356e-05,0.0003793779761,0.0007304374012, 0.000908970891,
  0.0008397826459,0.0005757701583,0.0002562592272,3.523997657e-05
	};

// 界面
void Drawinterface(void)
{
	u16 k;

	LCD_Line_H(0,161,320,LCD_COLOR_BLACK);	// 画一条实横线
	
	for(k=0;k<7;k++)
		LCD_DotLine_H(0,(k+1)*20,320,LCD_COLOR_BLACK,LCD_COLOR_WHITE);	// 画虚横线
	for(k=0;k<15;k++)
		LCD_DotLine_V((k+1)*20,0,160,LCD_COLOR_BLACK,LCD_COLOR_WHITE);	// 画虚竖线
}
	
int main(void)
{
	arm_fir_instance_f32 S;
	
	u16 j=0,k=0;
	u8 but[64];
	u16 c1,c2,Draw_H;		// 点对应的液晶位置，与两点之间的距离
	u32 p_num=0;	  		// 用于刷新最大值和最小值
	u32 min[2]={0xFFFFFFFF,0xFFFFFFFF};
	u32 max[2]={0,0};
	u32 Peak;						// 峰峰值
	u32 BPM_LH[3];			// 用于判断波峰
	u16 multiple=250;		// 液晶屏显示时波形的衰减倍数，模拟器衰减80倍，人体衰减250倍
	float BPM;					// 心率
	
	flog=0;
	i=0;
	
  PeripheralInit(); // 外设初始化
	Drawinterface();
	LCD_WriteString(100, 200, LCD_COLOR_BLACK, LCD_COLOR_WHITE, (uint8_t *)"HR:         BPM");
	// 初始化结构体S
	arm_fir_init_f32(&S, NumTaps, (float32_t *)BPF_5Hz_40Hz, firState, blockSize);
	
	CS_L;
	Delay_1us(10);
  SPI1_ReadWriteByte(RDATAC);		// 发送启动连续读取数据命令
  Delay_1us(10);
	CS_H;						
  START_H; 				// 启动转换
	CS_L;
	// 显示波形前先获取最大值和最小值
	while(k<1000)
	{
		if(i==0)
		{
			for(j=0;j<Samples_Number;j++)
				Input_data[j]=(float32_t)Cache_data[j];
			// FIR滤波
			arm_fir_f32(&S, Input_data, Output_data, blockSize);
			// 比较大小
			for(j=0;j<Samples_Number;j++)
			{
				if(min[1]>Output_data[j])
					min[1]=Output_data[j];
				if(max[1]<Output_data[j])
					max[1]=Output_data[j];
			}
			// 刷新显示数据
			for(j=0;j<Draw_Number-Samples_Number;j++)
				Draw_data[j]=Draw_data[j+Samples_Number];
			for(j=0;j<Samples_Number;j++)
				Draw_data[Draw_Number-Samples_Number+j]=(u32)Output_data[j];
			// 每隔500个点重新测量一次最大最小值
			if(p_num>4000)
			{
				min[0]=min[1];			
				max[0]=max[1];
				min[1]=0xFFFFFFFF;
				max[1]=0;
				Peak=max[0]-min[0];
				multiple=Peak/120;
				p_num=0;
			}
			p_num+=Samples_Number;
			k+=Samples_Number;
		}
	}
	
  while (1)
  {	
		if(i==0)
		{
			for(j=0;j<Samples_Number;j++)
				Input_data[j]=(float32_t)Cache_data[j];
			// 实现FIR滤波
			arm_fir_f32(&S, Input_data, Output_data, blockSize);
			// 比较大小
			for(j=0;j<Samples_Number;j++)
			{
				if(min[1]>Output_data[j])
					min[1]=Output_data[j];
				if(max[1]<Output_data[j])
					max[1]=Output_data[j];	
			}
			
			// 寻找峰值，并计算心率
			for(j=0;j<Samples_Number;j++)
			{
				BPM_LH[0]=BPM_LH[1];
				BPM_LH[1]=BPM_LH[2];
				BPM_LH[2]=Output_data[j];
				if((BPM_LH[0]<BPM_LH[1])&(BPM_LH[1]>max[0]-Peak/3)&(BPM_LH[2]<BPM_LH[1]))
				{
					BPM=(float)60000.0/(float)((point_cnt+j-Samples_Number)*4);
					if(BPM<200)
					{
						sprintf((char *)but,"%8.3f",BPM);
						LCD_WriteString(124, 200, LCD_COLOR_RED, LCD_COLOR_WHITE, (uint8_t *)but);
					}
					point_cnt=Samples_Number-j-1;
				}
			}
			
			// 消除上一次显示的点
			for(j=0;j<Draw_Number-1;j++)
			{
				c1=160-((Draw_data[j])-min[0]+1000)/multiple;
				c2=160-((Draw_data[j+1])-min[0]+1000)/multiple;
				if(c1>=160)
					c1=159;
				if(c2>=160)
					c2=159;
				
				if(c1>c2)
				{
					Draw_H=c1-c2;
					LCD_Line_V(j,c2,Draw_H,LCD_COLOR_WHITE);
				}
				else if(c1<c2)
				{
					Draw_H=c2-c1;
					LCD_Line_V(j,c1,Draw_H,LCD_COLOR_WHITE);
				}
				else
				{
					LCD_SetPoint(j,c1,LCD_COLOR_WHITE);
				}
			}
			// 重新画网格
			Drawinterface();
			
			// 刷新显示数据
			for(j=0;j<Draw_Number-Samples_Number;j++)
				Draw_data[j]=Draw_data[j+Samples_Number];
			for(j=0;j<Samples_Number;j++)
				Draw_data[Draw_Number-Samples_Number+j]=(u32)Output_data[j];
			
			// 每隔2000个点重新测量一次最大最小值
			if(p_num>2000)
			{
				min[0]=min[1];			
				max[0]=max[1];
				min[1]=0xFFFFFFFF;
				max[1]=0;
				Peak=max[0]-min[0];
				multiple=Peak/120;
				p_num=0;
			}
			p_num+=Samples_Number;
			
			// 显示新的数据点
			for(j=0;j<Draw_Number-1;j++)
			{
				c1=160-(Draw_data[j]-min[0]+1000)/multiple;
				c2=160-(Draw_data[j+1]-min[0]+1000)/multiple;
				if(c1>=160)
					c1=159;
				if(c2>=160)
					c2=159;
				
				if(c1>c2)
				{
					Draw_H=c1-c2;
					LCD_Line_V(j,c2,Draw_H,LCD_COLOR_RED);
				}
				else if(c1<c2)
				{
					Draw_H=c2-c1;
					LCD_Line_V(j,c1,Draw_H,LCD_COLOR_RED);
				}
				else
				{
					LCD_SetPoint(j,c1,LCD_COLOR_RED);
				}
			}
		}
  }
}

//-----------------------------------------------------------------
// End Of File
//-----------------------------------------------------------------
