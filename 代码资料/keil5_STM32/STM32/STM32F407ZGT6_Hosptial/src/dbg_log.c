/*!
*  \file dbg_log.c
*  \brief PC 调试日志实现：USART1 + TX 环形缓冲 + TXE 中断
*
*  引脚：PA9 = USART1_TX，PA10 = USART1_RX（AF7），APB2 时钟，115200 8N1。
*  中断优先级：抢占 1 / 子 3（与串口屏 USART2 的 1/1 同组，日志让位屏幕）。
*
*  非阻塞说明：
*    Dbg_Log() 只做"格式化 -> 压 TX 缓冲 -> 打开 TXE 中断"，不等待发送完成；
*    真正的字节发送全部在 USART1_IRQHandler 里完成。
*    采集链（Meas_Tick，10ms 一拍）永远不会被日志拖住。
*/
#include "dbg_log.h"
#include "meas.h"
#include "platform_config.h"
#include <stdio.h>
#include <stdarg.h>

/* ==================== 配置 ==================== */
#define DBG_TX_BUF_SIZE     2048u       /* TX 环形缓冲，必须是 2 的幂 */
#define DBG_RAW_POP_MAX     8u          /* DbgLog_Task 每次最多弹出的样本数 */
#define DBG_LINE_BUF        160u        /* 单行格式化缓冲 */

/* ==================== TX 环形缓冲 ==================== */
static uint8  s_txbuf[DBG_TX_BUF_SIZE];
static volatile uint16 s_tx_w = 0;
static volatile uint16 s_tx_r = 0;
static uint32 s_tx_drop   = 0;          /* 缓冲满被丢的字节数 */

extern volatile uint32 timer_tick_count;    /* ulitity.c 的 10ms 节拍 */

/* 压一个字节，满则丢弃并计数 */
static void tx_push(uint8 b)
{
    uint16 next = (uint16)((s_tx_w + 1u) & (DBG_TX_BUF_SIZE - 1u));
    if (next == s_tx_r)
    {
        s_tx_drop++;
        return;
    }
    s_txbuf[s_tx_w] = b;
    s_tx_w = next;
}

/* ==================== 硬件初始化 ==================== */
void DbgLog_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef uart;
    NVIC_InitTypeDef  nvic;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    /* PA9 = TX，PA10 = RX，AF7 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9,  GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);

    gpio.GPIO_Pin   = GPIO_Pin_9 | GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &gpio);

    uart.USART_BaudRate            = 115200;
    uart.USART_WordLength          = USART_WordLength_8b;
    uart.USART_StopBits            = USART_StopBits_1;
    uart.USART_Parity              = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &uart);

    nvic.NVIC_IRQChannel                   = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 3;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART1, ENABLE);
    /* TXE 中断先不开，等缓冲里第一个字节进来时再开（见 Dbg_Log） */
}

/* ==================== 格式化输出 ==================== */
void Dbg_Log(const char *fmt, ...)
{
    char    line[DBG_LINE_BUF];
    va_list ap;
    int     n, i;

    va_start(ap, fmt);
    n = vsnprintf(line, DBG_LINE_BUF - 2, fmt, ap);
    va_end(ap);

    if (n < 0) return;
    if (n > (int)(DBG_LINE_BUF - 3)) n = (int)(DBG_LINE_BUF - 3);

    for (i = 0; i < n; i++) tx_push((uint8)line[i]);
    tx_push('\r');
    tx_push('\n');

    /* 缓冲里有东西了，打开 TXE 中断让 ISR 发出去 */
    USART_ITConfig(USART1, USART_IT_TXE, ENABLE);
}

/* ==================== 日志任务 ==================== */
void DbgLog_Task(void)
{
#if DBG_LOG_RAW_EN
    raw_sample_t s;
    uint8 i;

    for (i = 0; i < DBG_RAW_POP_MAX; i++)
    {
        if (!Meas_SamplePop(&s)) break;
        Dbg_Log("[S] seq=%lu t=%lums r=%ld i=%ld",
                s.seq, s.t_ms, s.red, s.ir);
    }
#endif

    /* 每 1s 一行状态（timer_tick_count = 10ms 一拍） */
    {
        static uint32 s_last_st = 0;
        uint32 now = timer_tick_count;

        if ((now - s_last_st) >= 100u)
        {
            s_last_st = now;
            Dbg_Log("[ST] t=%lus spo2=%d pr=%d q=%u probe=%u valid=%u fault=%u sdrop=%lu txdrop=%lu",
                    now / 100u,
                    (int)g_meas.spo2, (int)g_meas.pulse_rate,
                    (unsigned)g_meas.quality, (unsigned)g_meas.probe,
                    (unsigned)g_meas.valid, (unsigned)g_meas.fault_code,
                    Meas_SampleDrops(), s_tx_drop);
        }
    }
}

uint32 DbgLog_TxDrops(void)
{
    return s_tx_drop;
}

/* ==================== 发送中断 ====================
*  TXE：缓冲非空则发下一字节，发空后关 TXE 中断（等下次 Dbg_Log 再开）。
*  定义在本文件而不是 stm32f4xx_it.c，保持日志模块自包含。 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_TXE) != RESET)
    {
        if (s_tx_r != s_tx_w)
        {
            USART_SendData(USART1, s_txbuf[s_tx_r]);
            s_tx_r = (uint16)((s_tx_r + 1u) & (DBG_TX_BUF_SIZE - 1u));
        }
        else
        {
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
        }
    }
}
