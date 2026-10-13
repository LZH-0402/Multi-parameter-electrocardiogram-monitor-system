/**
 * hmi_dacai.c - 大彩组态串口屏指令封装（2026-10-09 帧格式修正版）
 *
 * 正确的 B1 子指令（与《大彩指令集》+ 本机 SPO2 工程 hmi_tool.py 一致）：
 *   B1 00 <画面:16>                      切换画面
 *   B1 01                                查询当前画面（屏会应答）
 *   B1 10 <画面:16> <控件:16> <文本>      写文本控件
 *   B1 11 <画面:16> <控件:16>             读控件值
 *   B1 32 <画面:16> <控件:16> <通道:8> <点数:16> <数据>  曲线灌数据
 *
 * 旧版错误：把切页写成 "EE B1 <页号>"，实际 B1 01 是"查询画面"，所以屏一直不动。
 */
#include "hmi_dacai.h"
#include <string.h>

static hmi_send_cb_t g_send = 0;

/* 大彩帧尾 */
static const uint8_t TAIL[4] = {0xFF, 0xFC, 0xFF, 0xFF};

/* 是否启用 CRC16 校验，必须与屏工程 BP_Hospital.tftprj 的 DeviceEnableCRC 一致。
 * 本工程屏 DeviceEnableCRC="0" -> 不启用（默认 0）。
 * 若把屏改成启用 CRC，这里改 1 即可。 */
#ifndef HMI_USE_CRC
#define HMI_USE_CRC 0
#endif

void hmi_init(hmi_send_cb_t send)
{
    g_send = send;
}

#if HMI_USE_CRC
/* CRC-16/MODBUS：只对 body(命令+参数) 计算，不含帧头 EE、不含帧尾 FF FC FF FF。
 * 例：body = B1 01 -> CRC = 0x20B4，按"高字节在前"写入即 20 B4。 */
static uint16_t hmi_crc16(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}
#endif

/* 内部：发送 0xEE + body + [CRC] + 帧尾 */
void hmi_send_frame(const uint8_t *body, uint16_t body_len)
{
    uint8_t head = 0xEE;
    if (!g_send) return;
    g_send(&head, 1);
    g_send(body, body_len);
#if HMI_USE_CRC
    {
        uint16_t c = hmi_crc16(body, body_len);
        uint8_t  cb[2];
        cb[0] = (uint8_t)(c >> 8);   /* 高字节在前 */
        cb[1] = (uint8_t)(c & 0xFF);
        g_send(cb, 2);
    }
#endif
    g_send(TAIL, 4);
}

void hmi_change_page(uint16_t screen_id)
{
    /* 切换画面 = B1 00 + 画面号(2字节，高前低后) */
    uint8_t body[4];
    body[0] = 0xB1;
    body[1] = 0x00;
    body[2] = (uint8_t)(screen_id >> 8);
    body[3] = (uint8_t)(screen_id & 0xFF);
    hmi_send_frame(body, sizeof(body));
}

void hmi_get_screen(void)
{
    /* 查询当前画面 = B1 01（屏会回 EE B1 01 <画面:16> FF FC FF FF） */
    uint8_t body[2];
    body[0] = 0xB1;
    body[1] = 0x01;
    hmi_send_frame(body, sizeof(body));
}

void hmi_write_text(uint16_t screen_id, uint16_t ctrl_id, const char *str)
{
    uint8_t  body[192];
    uint16_t n   = 0;
    uint16_t len = (uint16_t)strlen(str);
    if (len > sizeof(body) - 6) len = sizeof(body) - 6;   /* 留 6 字节头 */

    body[n++] = 0xB1;
    body[n++] = 0x10;                       /* 10 = 写文本控件 */
    body[n++] = (uint8_t)(screen_id >> 8);
    body[n++] = (uint8_t)(screen_id & 0xFF);
    body[n++] = (uint8_t)(ctrl_id >> 8);
    body[n++] = (uint8_t)(ctrl_id & 0xFF);
    for (uint16_t i = 0; i < len; i++)
        body[n++] = (uint8_t)str[i];

    hmi_send_frame(body, n);
}

void hmi_get_control(uint16_t screen_id, uint16_t ctrl_id)
{
    uint8_t body[6];
    body[0] = 0xB1;
    body[1] = 0x11;                         /* 11 = 读控件值 */
    body[2] = (uint8_t)(screen_id >> 8);
    body[3] = (uint8_t)(screen_id & 0xFF);
    body[4] = (uint8_t)(ctrl_id >> 8);
    body[5] = (uint8_t)(ctrl_id & 0xFF);
    hmi_send_frame(body, sizeof(body));
}

void hmi_draw_curve(uint16_t screen_id, uint16_t ctrl_id, uint8_t channel,
                    const uint8_t *data, uint16_t len)
{
    uint8_t  body[96];
    uint16_t n = 0;
    if (len > sizeof(body) - 9) len = sizeof(body) - 9;

    body[n++] = 0xB1;
    body[n++] = 0x32;                       /* 32 = 曲线灌数据 */
    body[n++] = (uint8_t)(screen_id >> 8);
    body[n++] = (uint8_t)(screen_id & 0xFF);
    body[n++] = (uint8_t)(ctrl_id >> 8);
    body[n++] = (uint8_t)(ctrl_id & 0xFF);
    body[n++] = channel;
    body[n++] = (uint8_t)(len >> 8);
    body[n++] = (uint8_t)(len & 0xFF);
    for (uint16_t i = 0; i < len; i++)
        body[n++] = data[i];

    hmi_send_frame(body, n);
}

/* ---- 背光 / 蜂鸣（★2026-10-09 用《大彩指令集 V5.0/V5.1》核实指令码）----
 *   背光调节 0x60 : 参数 Light_level 1 字节，【0x00=最亮 / 0xFF=背光关闭】（值越大越暗）
 *   蜂鸣器   0x61 : 参数 Time 1 字节，单位 10ms
 *   例：设 50% 亮度（=中暗）→ EE 60 7F FF FC FF FF
 *   注意：不建议把 level 设为 0xFF(全灭)，部分屏需触摸才恢复。 */
void hmi_set_backlight(uint8_t level)
{
    uint8_t body[2];
    body[0] = 0x60;                 /* 0x60 = 背光调节（不是 0x90） */
    body[1] = level;                /* 0=最亮 .. 255=最暗 */
    hmi_send_frame(body, sizeof(body));
}

void hmi_beep(uint16_t ms)
{
    uint8_t body[2];
    body[0] = 0x61;                 /* 0x61 = 蜂鸣器 */
    body[1] = (uint8_t)((ms / 10u) > 255u ? 255u : (ms / 10u));  /* 单位 10ms，1 字节 */
    hmi_send_frame(body, sizeof(body));
}
