/**
 * hmi_dacai.c - 大彩组态串口屏指令封装（2026-10-09 帧格式修正版）
 *
 * 正确的 B1 子指令（与《大彩指令集》+ 本机 SPO2 工程 hmi_tool.py 一致）：
 *   B1 00 <画面:16>                      切换画面
 *   B1 01                                查询当前画面（屏会应答）
 *   B1 10 <画面:16> <控件:16> <文本>      写文本控件
 *   B1 11 <画面:16> <控件:16>             读控件值
 *   B1 32 <画面:16> <控件:16> <通道:8> <点数:16> <数据>  曲线灌数据
 *   81    <秒><分><时><日><星期><月><年>（BCD）        设置屏内 RTC
 *
 * 旧版错误：把切页写成 "EE B1 <页号>"，实际 B1 01 是"查询画面"，所以屏一直不动。
 */
#include "hmi_dacai.h"
#include <string.h>

static hmi_send_cb_t g_send = 0;

/* “当前可见页变化”回调（app_bp 注册，用于新页落地立即补刷时钟） */
static hmi_page_changed_cb_t g_page_changed_cb = 0;

void hmi_set_page_changed_cb(hmi_page_changed_cb_t cb)
{
    g_page_changed_cb = cb;
}

void hmi_fire_page_changed(uint16_t screen_id)
{
    if (g_page_changed_cb) g_page_changed_cb(screen_id);
}

/* 当前屏可见页号（任务D修复用）：
 *   - MCU 主动切页时由 hmi_change_page() 写入；
 *   - 屏自行切页(切页型按钮)时，由 app_bp.c 周期 hmi_get_screen() 回读的
 *     B1 01 应答同步到 g_hmi_cur_screen。
 * 用于"只刷新可见页"，把每秒 10 帧的写文本广播降到 1 帧，
 * 避免大彩屏串口解析器在连续帧流中偶发黏帧→把乱码渲染成 "？？"。 */
uint16_t g_hmi_cur_screen = 0;

/* 大彩帧尾 */
static const uint8_t TAIL[4] = {0xFF, 0xFC, 0xFF, 0xFF};

/* CRC 开关：定义已移到 hmi_dacai.h，当前 HMI_USE_CRC=1 已启用。
 * MCU 发往屏的每帧都带 CRC-16/MODBUS；屏工程 BP_Hospital.tftprj / SPO2_Hospital.tftprj
 * 已同步设 DeviceEnableCRC="1"。启用后偶发坏帧被屏直接丢弃(保留上次值)，不再渲染成 "？？"。 */

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
    g_hmi_cur_screen = screen_id;   /* ★任务D：MCU 主动切页，记录当前可见页 */
    hmi_fire_page_changed(screen_id);  /* ★时钟补刷：新页落地立即显示正确时间 */
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

/* ---- 设置屏内 RTC：EE 81 <秒><分><时><日><星期><月><年>（BCD）----
 * 字段顺序与血氧屏 dc_hmi 的 HMI_SetRTCTime() 完全一致，两块屏可"同一时刻发同一条指令"。
 * 帧长固定 = 1(EE) + 1(81) + 7(BCD) + 2(CRC) + 4(尾) = 15 字节 ≈ 15.6ms @9600。
 * 屏收到后立即把 RTC 计数器改为给定值并继续自走；此后只需周期性重发校准。 */
static uint8_t bin2bcd(uint8_t v)
{
    /* 入参都是 0~99 的十进制数，直接按 十位/个位 拆（比位运算更省且不依赖 CPU 行为） */
    if (v > 99u) v = (uint8_t)(v % 100u);
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

void hmi_set_rtc(uint8_t year2, uint8_t month, uint8_t day,
                 uint8_t hour, uint8_t minute, uint8_t second, uint8_t week)
{
    uint8_t body[8];
    body[0] = 0x81;                 /* 0x81 = 设置 RTC */
    body[1] = bin2bcd(second);
    body[2] = bin2bcd(minute);
    body[3] = bin2bcd(hour);
    body[4] = bin2bcd(day);
    body[5] = bin2bcd(week);
    body[6] = bin2bcd(month);
    body[7] = bin2bcd(year2);
    hmi_send_frame(body, sizeof(body));
}
