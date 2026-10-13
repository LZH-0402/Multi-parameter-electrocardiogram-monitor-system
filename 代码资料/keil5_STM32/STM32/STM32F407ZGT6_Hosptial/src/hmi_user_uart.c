/******************************************************************************
**-----------------------------------文件信息--------------------------------------------
** 文件名称:   hmi_user_uart.c
** 文件说明:   用户MCU串口驱动函数库
** 芯片：      STM32F407ZGT6
**   1) 时钟：RCC_APB2PeriphClockCmd(GPIOA|AFIO) -> RCC_AHB1PeriphClockCmd(GPIOA)
**            + RCC_APB1PeriphClockCmd(USART2)
**   2) GPIO：F4 无 AFIO，改为 GPIO_Mode_AF + GPIO_PinAFConfig(GPIO_AF_USART2)
**            并补充 F4 特有的 GPIO_OType / GPIO_PuPd 成员
**   3) 修复原工程 Bug：UartInit 初始化 USART2，而 SendChar/中断却用 USART1，
**            现在三者统一为 platform_config.h 中的 HMI_USART(USART2)
**--------------------------------------------------------------------------------------
** 移植到其他平台时，只需实现 UartInit() 与 SendChar()，不要修改函数名称，
** 否则无法与 hmi_driver.c 匹配。
--------------------------------------------------------------------------------------*/
#include "hmi_user_uart.h"
#include "platform_config.h"


/*!
*   \brief   串口初始化
*   \param   BaudRate-波特率设置
*/
void UartInit(uint32 BaudRate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    /* 1. 开时钟：F4 的 GPIO 挂在 AHB1 上，USART2 挂在 APB1 上 */
    RCC_AHB1PeriphClockCmd(HMI_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(HMI_USART_CLK, ENABLE);

    /* 2. 引脚复用映射：F4 没有 AFIO 模块，必须用 GPIO_PinAFConfig */
    GPIO_PinAFConfig(HMI_GPIO_PORT, HMI_TX_PINSOURCE, HMI_GPIO_AF);
    GPIO_PinAFConfig(HMI_GPIO_PORT, HMI_RX_PINSOURCE, HMI_GPIO_AF);

    /* 3. 引脚配置：复用功能 + 推挽 + 上拉 + 50MHz */
	// USART2_TX--PA2,USART2_RX--PA3,
    GPIO_InitStructure.GPIO_Pin   = HMI_TX_PIN | HMI_RX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(HMI_GPIO_PORT, &GPIO_InitStructure);

    /* 4. 串口参数 */
    USART_DeInit(HMI_USART);
    USART_InitStructure.USART_BaudRate            = BaudRate;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(HMI_USART, &USART_InitStructure);

    /* 5. 接收中断 + 使能串口 */
    USART_ITConfig(HMI_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(HMI_USART, ENABLE);
}

/*!
*   \brief  发送1个字节
*   \param  t 发送的字节
*/
void  SendChar(uchar t)
{
    USART_SendData(HMI_USART, t);
    while(USART_GetFlagStatus(HMI_USART, USART_FLAG_TXE) == RESET);
    while(USART_GetFlagStatus(HMI_USART, USART_FLAG_TC) == RESET);   //等待发送完毕
}

/*************************** END OF FILE **************************************/
