/**
 * bp_sensor.c - HKB-08B+ 血压传感器 UART 协议实现（纯 C，无 SDK 依赖）
 *
 * 硬件通道: 传感器接 UART5@115200；接收中断里逐字节喂给 bp_sensor_feed()。
 */
#include "bp_sensor.h"

/* 解析器状态机（帧: FF C0 | LEN | CKSUM | CMD | DATA...） */
typedef enum {
    S_WAIT_FF = 0,  /* 等帧头 0xFF */
    S_WAIT_C0,      /* 等帧头 0xC0 */
    S_LEN,          /* 收 LEN */
    S_CKSUM,        /* 收 CKSUM（该字节本身不参与累加） */
    S_CMD,          /* 收 CMD */
    S_DATA          /* 收 DATA */
} bp_state_t;

static bp_state_t   g_state = S_WAIT_FF;
static uint8_t      g_len;        /* LEN 值 = 3 + DATA 长度 */
static uint8_t      g_cksum_recv; /* 收到的 CKSUM */
static uint8_t      g_cmd;        /* 命令字 */
static uint8_t      g_data[32];   /* DATA 缓冲，最多 32 字节 */
static uint8_t      g_idx;        /* DATA 已收个数 */
static uint8_t      g_sum;        /* 校验和运行和(含 LEN、CMD、DATA) */
static uint8_t      g_expect;     /* 期望 DATA 个数 = LEN-3，越界时取 0 */
static bp_frame_cb_t g_cb = 0;

/* 初始化解析器：复位状态机、清 DATA 计数并注册整帧回调 cb（cb 可为 0） */
void bp_sensor_init(bp_frame_cb_t cb)
{
    g_state = S_WAIT_FF;
    g_idx = 0;
    g_cb = cb;
}

/* 逐字节喂入解析器；在 UART 接收中断中调用。
 * 收满一帧且校验通过时回调 g_cb（处于中断上下文，回调须短小、勿阻塞）。 */
void bp_sensor_feed(uint8_t b)
{
    switch (g_state) {
    case S_WAIT_FF:
        if (b == 0xFF) g_state = S_WAIT_C0;
        break;

    case S_WAIT_C0:
        if (b == 0xC0) {
            g_state = S_LEN;
            g_sum = 0;          /* 校验和从 LEN 开始累加 */
        } else if (b == 0xFF) {
            g_state = S_WAIT_C0; /* 连续 FF，保持等待 C0 */
        } else {
            g_state = S_WAIT_FF;
        }
        break;

    case S_LEN:
        g_len = b;
        g_sum = (g_sum + b) & 0xFF;
        g_expect = (g_len >= 3) ? (g_len - 3) : 0;   /* DATA = LEN-3，负数按 0 */
        g_idx = 0;
        g_state = S_CKSUM;
        break;

    case S_CKSUM:
        /* CKSUM 字节本身不参与累加：
         * CKSUM = (LEN + CMD + sum(DATA)) & 0xFF，此处只记录收到的值，
         * g_sum 仍为 S_LEN 起的累加和。 */
        g_cksum_recv = b;
        g_state = S_CMD;
        break;

    case S_CMD:
        g_cmd = b;
        g_sum = (g_sum + b) & 0xFF;
        if (g_expect == 0) {
            /* 无 DATA，CMD 收完即刻校验 */
            if (g_sum == g_cksum_recv && g_cb)
                g_cb(g_cmd, g_data, 0);
            g_state = S_WAIT_FF;
        } else {
            g_state = S_DATA;
        }
        break;

    case S_DATA:
        if (g_idx < sizeof(g_data)) {   /* 超出 32 字节则丢弃，g_idx 仍递增以判帧尾 */
            g_data[g_idx] = b;
            g_sum = (g_sum + b) & 0xFF;
        }
        g_idx++;
        if (g_idx >= g_expect) {
            if (g_sum == g_cksum_recv && g_cb)
                g_cb(g_cmd, g_data, g_expect);
            g_state = S_WAIT_FF;
        }
        break;

    default:
        g_state = S_WAIT_FF;
        break;
    }
}

/* 组帧并回写帧长度：*out_len = 帧总字节数（= 5 + data_len），out 空间同 bp_make_cmd 要求 */
void bp_build_cmd(uint8_t *out, uint8_t *out_len,
                  uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    *out_len = bp_make_cmd(out, cmd, data, data_len);
}

/* 组帧: FF C0 | LEN | CKSUM | CMD | DATA...
 * data 为 NULL 且 data_len=0 时只发头部；返回帧总长度 = 5 + data_len，
 * 调用方需保证 buf 容量 >= 5 + data_len。data_len 最大 252（LEN 为单字节且 =3+N）。 */
uint8_t bp_make_cmd(uint8_t *buf, uint8_t cmd,
                    const uint8_t *data, uint8_t data_len)
{
    uint8_t len = (uint8_t)(3 + data_len);
    uint8_t cksum = len;          /* 校验和含 LEN */
    cksum = (cksum + cmd) & 0xFF;
    for (uint8_t i = 0; i < data_len; i++)
        cksum = (cksum + data[i]) & 0xFF;

    buf[0] = 0xFF;
    buf[1] = 0xC0;
    buf[2] = len;
    buf[3] = cksum;
    buf[4] = cmd;
    for (uint8_t i = 0; i < data_len; i++)
        buf[5 + i] = data[i];
    return (uint8_t)(5 + data_len);
}

/* 解析测量结果 0xAC: d[0..4] = SSYH SSYL SZYH SZYL XL
 *   收缩压 = ((SSYH & 0x7F)<<8)|SSYL   mmHg（SSYH.7 为心率不齐位，需屏蔽）
 *   舒张压 = (SZYH<<8)|SZYL            mmHg
 *   心率   = XL                        bpm
 *   不齐   = SSYH.7，1=心率不齐 */
void bp_parse_result(const uint8_t *d, bp_result_t *r)
{
    r->systolic  = ((uint16_t)(d[0] & 0x7F) << 8) | d[1]; /* 屏蔽 SSYH.7 心率不齐位 */
    r->diastolic = ((uint16_t)d[2] << 8) | d[3];
    r->heart_rate = d[4];
    r->irregular  = (d[0] >> 7) & 0x01;
}

/* 解析实时气压 0xA0 应答: d[0..1] = QYH QYL（测量中约 10Hz）
 *   气压 = ((QYH & 0xEF)<<8)|QYL  mmHg（QYH.4 为心跳位，需屏蔽）
 *   心跳 = QYH.4，1=本次采样有心跳搏动 */
void bp_parse_pressure(const uint8_t *d, bp_pressure_t *p)
{
    p->pressure = ((uint16_t)(d[0] & 0xEF) << 8) | d[1]; /* 屏蔽 QYH.4 心跳位 */
    p->has_beat = (d[0] >> 4) & 0x01;
}

/* 解析波形点 0xD3: d[0..1] = waveH waveL（静压模式 200Hz）
 *   value = (waveH<<8)|waveL，为波形原始值，无单位 */
void bp_parse_wave(const uint8_t *d, bp_wave_t *w)
{
    w->value = ((uint16_t)d[0] << 8) | d[1];
}
