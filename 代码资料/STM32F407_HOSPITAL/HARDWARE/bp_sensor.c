/**
 * bp_sensor.c - HKB-08B+ 血压传感器协议实现（纯 C，无 SDK 依赖）
 */
#include "bp_sensor.h"

/* 解析器状态机 */
typedef enum {
    S_WAIT_FF = 0,  /* 等待 0xFF */
    S_WAIT_C0,      /* 等待 0xC0 */
    S_LEN,          /* 收 LEN */
    S_CKSUM,        /* 收 CKSUM */
    S_CMD,          /* 收 CMD */
    S_DATA          /* 收 DATA */
} bp_state_t;

static bp_state_t   g_state = S_WAIT_FF;
static uint8_t      g_len;        /* LEN 值 */
static uint8_t      g_cksum_recv; /* 收到的 CKSUM */
static uint8_t      g_cmd;        /* 命令字 */
static uint8_t      g_data[32];   /* DATA 缓冲 */
static uint8_t      g_idx;        /* DATA 已收个数 */
static uint8_t      g_sum;        /* 校验和运行和(含 LEN) */
static uint8_t      g_expect;     /* 期望 DATA 个数 = LEN-3 */
static bp_frame_cb_t g_cb = 0;

void bp_sensor_init(bp_frame_cb_t cb)
{
    g_state = S_WAIT_FF;
    g_idx = 0;
    g_cb = cb;
}

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
            g_state = S_WAIT_C0; /* 连续 FF，保持 */
        } else {
            g_state = S_WAIT_FF;
        }
        break;

    case S_LEN:
        g_len = b;
        g_sum = (g_sum + b) & 0xFF;
        g_expect = (g_len >= 3) ? (g_len - 3) : 0;
        g_idx = 0;
        g_state = S_CKSUM;
        break;

    case S_CKSUM:
        /* ★ 修复校验和 BUG：CKSUM 字节本身【不】参与累加。
         *   手册 CKSUM0 = (LEN + CMD + sum(DATA)) & 0xFF，不含 CKSUM 自身。
         *   旧代码在这里 g_sum += b 把 CKSUM 自己算进去了，导致所有合法帧
         *   校验失败被丢弃。这里只记录收到的值，g_sum 保持 S_LEN 起的 LEN。 */
        g_cksum_recv = b;
        g_state = S_CMD;
        break;

    case S_CMD:
        g_cmd = b;
        g_sum = (g_sum + b) & 0xFF;
        if (g_expect == 0) {
            /* 无 DATA，直接校验 */
            if (g_sum == g_cksum_recv && g_cb)
                g_cb(g_cmd, g_data, 0);
            g_state = S_WAIT_FF;
        } else {
            g_state = S_DATA;
        }
        break;

    case S_DATA:
        if (g_idx < sizeof(g_data)) {
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

void bp_build_cmd(uint8_t *out, uint8_t *out_len,
                  uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    *out_len = bp_make_cmd(out, cmd, data, data_len);
}

uint8_t bp_make_cmd(uint8_t *buf, uint8_t cmd,
                    const uint8_t *data, uint8_t data_len)
{
    uint8_t len = (uint8_t)(3 + data_len);
    uint8_t cksum = len;          /* 含 LEN */
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

/* 解析测量结果 0xAC: SSYH SSYL SZYH SZYL XL */
void bp_parse_result(const uint8_t *d, bp_result_t *r)
{
    r->systolic  = ((uint16_t)(d[0] & 0x7F) << 8) | d[1]; /* 屏蔽 SSYH.7 心率不齐位 */
    r->diastolic = ((uint16_t)d[2] << 8) | d[3];
    r->heart_rate = d[4];
    r->irregular  = (d[0] >> 7) & 0x01;
}

/* 解析实时气压 0xA0 应答: QYH QYL（测量中约10Hz） */
void bp_parse_pressure(const uint8_t *d, bp_pressure_t *p)
{
    p->pressure = ((uint16_t)(d[0] & 0xEF) << 8) | d[1]; /* 屏蔽 QYH.4 心跳位 */
    p->has_beat = (d[0] >> 4) & 0x01;
}

/* 解析波形点 0xD3: waveH waveL（静压模式 200Hz） */
void bp_parse_wave(const uint8_t *d, bp_wave_t *w)
{
    w->value = ((uint16_t)d[0] << 8) | d[1];
}
