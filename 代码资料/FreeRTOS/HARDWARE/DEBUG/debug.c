#include "stm32f4xx.h"
#include "debug.h"

#define DBG_BAUD   115200

void DEBUG_UART_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9,  GPIO_AF_USART1);   /* PA9 = TX */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);   /* PA10 = RX */

    gpio.GPIO_Pin   = GPIO_Pin_9 | GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = DBG_BAUD;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
}

void dbg_putc(uint8_t c)
{
    USART_SendData(USART1, c);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    {
    }
}

void dbg_puts(const char *s)
{
    while (s && *s)
    {
        dbg_putc((uint8_t)*s++);
    }
}

void dbg_put_u32(uint32_t v)
{
    char buf[12];
    int i = 0;

    if (v == 0)
    {
        dbg_putc('0');
        return;
    }
    while (v && i < 11)
    {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
    {
        dbg_putc((uint8_t)buf[--i]);
    }
}

void dbg_put_hex8(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    dbg_putc((uint8_t)hex[(v >> 4) & 0x0F]);
    dbg_putc((uint8_t)hex[v & 0x0F]);
}

void dbg_put_u32_hex(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;

    for (i = 7; i >= 0; i--)
    {
        dbg_putc((uint8_t)hex[(v >> (i * 4)) & 0xF]);
    }
}

/* printf 重定向：在 Keil 工程选项里勾选 "Use MicroLIB" 后，
   直接使用 printf(...) 即可把内容输出到 USART1（PA9）。
   若未勾选 MicroLIB 且未调用 printf，则此函数不会被链接，无影响。 */
int fputc(int ch, FILE *f)
{
    (void)f;
    dbg_putc((uint8_t)ch);
    return ch;
}
