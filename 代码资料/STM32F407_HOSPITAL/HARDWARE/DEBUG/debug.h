#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdint.h>
#include <stdio.h>   /* 为 fputc / printf 重定向（勾选 MicroLIB 后可用 printf） */

/* 调试串口：USART1，PA9(TX)/PA10(RX)，115200。
   仅用发送即可完成本工程所有验证；接收中断可后续扩展命令。 */
void DEBUG_UART_Init(void);
void dbg_putc(uint8_t c);
void dbg_puts(const char *s);
void dbg_put_u32(uint32_t v);
void dbg_put_u32_hex(uint32_t v);
void dbg_put_hex8(uint8_t v);   /* 单字节十六进制，用于原始字节抓包 */

#endif /* __DEBUG_H */
