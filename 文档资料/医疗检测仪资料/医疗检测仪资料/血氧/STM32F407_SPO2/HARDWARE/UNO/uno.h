#ifndef __UNO_H
#define __UNO_H

#include <stdint.h>

/* 与《代码开发设计步骤》第 3 节一致的测量结构 */
typedef struct {
    int32_t  red_raw;        /* RED 通道原始 ADC（来自 AFE4490 LED2VAL） */
    int32_t  ir_raw;         /* IR  通道原始 ADC（来自 AFE4490 LED1VAL） */
    int16_t  spo2;           /* 血氧 0~100；0 表示无效 / 探头错误 */
    int16_t  pulse_rate;     /* 脉率 PR，bpm */
    uint8_t  signal_quality; /* 灌注质量 0~100（当前为占位值，见文档 8.1） */
    uint8_t  probe_state;    /* 0 未接入 / 错误，1 接入 */
    uint8_t  result_valid;   /* 1 = 数值有效 */
    uint8_t  fault_code;
    uint32_t monitor_sec;    /* 监护时长（秒），由 main 累加 */
} measurement_t;

extern measurement_t g_meas;     /* 解析结果，应用层读取 */
extern uint8_t      g_new_frame; /* 收到一帧置 1，main 消费后清 0 */

/* 接收诊断：用于在 USART1 上判断"UNO 到底有没有在发" */
extern volatile uint32_t g_uno_rx_bytes;  /* USART6 收到的原始字节总数 */
extern volatile uint32_t g_uno_frames;    /* 解析成功的完整帧数 */

/* 每收到 UNO 一个字节调用一次（在 USART6 中断里） */
void UNO_ParseByte(uint8_t b);

/* 复位解析状态机与诊断计数（切换波特率前调用） */
void UNO_Reset(void);

/* 取最近 n 个原始字节（按接收先后顺序），返回实际拷贝个数。用于抓包回显。 */
uint8_t UNO_Snapshot(uint8_t *out, uint8_t n);

#endif /* __UNO_H */
