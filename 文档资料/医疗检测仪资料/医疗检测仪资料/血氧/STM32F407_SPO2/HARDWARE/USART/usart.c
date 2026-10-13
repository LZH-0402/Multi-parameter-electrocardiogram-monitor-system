#include "stm32f4xx.h"
#include "usart.h"
#include "dc_hmi.h"   /* HMI_RxByte：屏上行通知 */
#include "uno.h"      /* UNO_ParseByte：UNO 帧解析 */

/* ============================================================
 * 屏：USART2（PA2/PA3，APB1）
 * ============================================================ */
void USART2_Screen_Init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);   /* PA2 = TX */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);   /* PA3 = RX */

    gpio.GPIO_Pin   = GPIO_Pin_2 | GPIO_Pin_3;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &usart);
    USART_Cmd(USART2, ENABLE);

    /* 开 RXNE 中断，收到屏的按键/通知字节时喂给 dc_hmi 的帧收集器 */
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    nvic.NVIC_IRQChannel                   = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 1;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

/* ============================================================
 * UNO：USART6（PC6/PC7，APB2）
 * ============================================================ */
void USART6_UNO_Init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART6, ENABLE);

    GPIO_PinAFConfig(GPIOC, GPIO_PinSource6, GPIO_AF_USART6);   /* PC6 = TX（-> UNO D0，当前不用） */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF_USART6);   /* PC7 = RX（<- UNO D1） */

    gpio.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gpio);

    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART6, &usart);
    USART_Cmd(USART6, ENABLE);

    USART_ITConfig(USART6, USART_IT_RXNE, ENABLE);
    nvic.NVIC_IRQChannel                   = USART6_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

/* ============================================================
 * 屏驱动发送：USART2 轮询发送（简单可靠；要提速可改 DMA）
 * ============================================================ */
void hmi_tx(const uint8_t *p, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++) {
        USART_SendData(USART2, p[i]);
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
        }
    }
}

/* ============================================================
 * 中断：屏 -> MCU（上行通知）
 * ============================================================ */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(USART2);
        HMI_RxByte(b);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

/* ============================================================
 * 中断：UNO -> MCU（帧解析）
 * ============================================================ */
void USART6_IRQHandler(void)
{
    if (USART_GetITStatus(USART6, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(USART6);
        UNO_ParseByte(b);
        USART_ClearITPendingBit(USART6, USART_IT_RXNE);
    }
}
