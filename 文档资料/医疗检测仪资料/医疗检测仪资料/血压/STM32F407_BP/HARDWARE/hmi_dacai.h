/**
 * hmi_dacai.h - 大彩(Dacai)组态串口屏指令封装
 *
 * ★ 2026-10-09 重大修正（帧格式纠错）
 *   旧代码把"切页"写成 EE B1 <页号> FF FC FF FF —— 这是错的，
 *   B1 01 在大彩协议里是"查询当前画面"，不是切页，所以屏一直没反应。
 *   正确帧（与《大彩指令集》及本机 SPO2 工程 hmi_tool.py 完全一致）：
 *     切画面 : EE B1 00 <画面号:16>                 FF FC FF FF
 *     写文本 : EE B1 10 <画面号:16> <控件ID:16> <字符串> FF FC FF FF
 *     读控件 : EE B1 11 <画面号:16> <控件ID:16>      FF FC FF FF
 *     查画面 : EE B1 01                            FF FC FF FF  (屏会应答)
 *     曲线   : EE B1 32 <画面:16> <控件:16> <通道:8> <点数:16> <数据...> FF FC FF FF
 *   16/32 位参数一律"高字节在前"。
 *
 * CRC：本工程屏 DeviceEnableCRC="0"（不启用），故帧内不带 CRC。
 *      若将来把屏改成启用 CRC，把 hmi_dacai.c 里 HMI_USE_CRC 改成 1 即可
 *      （算法 CRC-16/MODBUS，只对"命令+参数"算，插在帧尾前，高字节在前）。
 *
 * 本文件不依赖 MCU SDK，send 通过回调输出（接到 USART3(屏) 发送函数）。
 */
#ifndef HMI_DACAI_H
#define HMI_DACAI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 发送回调: 把 buf 通过 USART3(屏) 发出去 */
typedef void (*hmi_send_cb_t)(const uint8_t *buf, uint16_t len);

void hmi_init(hmi_send_cb_t send);

/* 底层：发一帧  EE <body...> [CRC] FF FC FF FF （body = 命令+参数） */
void hmi_send_frame(const uint8_t *body, uint16_t body_len);

/* 切换画面: EE B1 00 <画面号:16> */
void hmi_change_page(uint16_t screen_id);

/* 查询当前画面: EE B1 01。屏应答 EE B1 01 <画面号:16> FF FC FF FF
 * —— 这是唯一能自动判定"接线 + 波特率 + 帧格式"三项都正确的指令。 */
void hmi_get_screen(void);

/* 写文本(ASCII/GBK): EE B1 10 <画面:16> <控件:16> <字符串> */
void hmi_write_text(uint16_t screen_id, uint16_t ctrl_id, const char *str);

/* 读控件值: EE B1 11 <画面:16> <控件:16>（屏应答控件当前值，可用来确认文本真写进去了） */
void hmi_get_control(uint16_t screen_id, uint16_t ctrl_id);

/* 曲线灌数据: EE B1 32 <画面:16> <控件:16> <通道:8> <点数:16> <每点1字节> */
void hmi_draw_curve(uint16_t screen_id, uint16_t ctrl_id, uint8_t channel,
                    const uint8_t *data, uint16_t len);

/* 背光 / 蜂鸣（★2026-10-09 按《大彩指令集 V5.0/V5.1》核实指令码）
 *   hmi_set_backlight(level): level = 0(最亮) .. 255(背光关闭)  —— 【值越大越暗】
 *   hmi_beep(ms)            : 蜂鸣 ms 毫秒（内部换算 10ms 单位，1 字节） */
void hmi_set_backlight(uint8_t level);
void hmi_beep(uint16_t ms);

#ifdef __cplusplus
}
#endif
#endif /* HMI_DACAI_H */
