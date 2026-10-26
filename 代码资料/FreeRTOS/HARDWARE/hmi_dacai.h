/**
 * hmi_dacai.h - 大彩(Dacai)组态串口屏(血压屏)下行指令封装
 *
 * 依赖: 无 MCU SDK；发送经 hmi_send_cb_t 回调输出（接到血压屏 USART3@9600 发送函数）。
 *
 * 帧格式（16/32 位参数一律"高字节在前"）：
 *   切画面 : EE B1 00 <画面号:16>                        FF FC FF FF
 *   写文本 : EE B1 10 <画面号:16> <控件ID:16> <字符串>     FF FC FF FF
 *   读控件 : EE B1 11 <画面号:16> <控件ID:16>             FF FC FF FF
 *   查画面 : EE B1 01                                   FF FC FF FF  (屏会应答)
 *   曲线   : EE B1 32 <画面:16> <控件:16> <通道:8> <点数:16> <数据...> FF FC FF FF
 *   设 RTC : EE 81 <秒> <分> <时> <日> <星期> <月> <年>   FF FC FF FF
 *            （均为 BCD；年 = 年份后两位；星期 0=周日..6=周六）
 *
 * CRC：HMI_USE_CRC=1 时每帧在帧尾前插入 CRC-16/MODBUS（只对"命令+参数"计算，高字节在前），
 *      屏工程 *.tftprj 的 DeviceEnableCRC 必须与之一致。
 */
#ifndef HMI_DACAI_H
#define HMI_DACAI_H

#include <stdint.h>

/* CRC 开关：1=启用，0=禁用。必须与屏工程 *.tftprj 的 DeviceEnableCRC 一致。
 *   只开 MCU 不开屏 → 屏把 CRC 字节当文本显示（如时钟末尾多 "&"）；
 *   只开屏不开 MCU → 屏整屏不显。 */
#ifndef HMI_USE_CRC
#define HMI_USE_CRC 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 发送回调: 把 buf 通过血压屏串口(USART3@9600)发出去 */
typedef void (*hmi_send_cb_t)(const uint8_t *buf, uint16_t len);

/* 注册发送回调；未注册时所有发送函数直接返回 */
void hmi_init(hmi_send_cb_t send);

/* 当前屏可见页号（由 hmi_dacai.c 维护，app_bp.c 读取，用于"只刷新可见页"） */
extern uint16_t g_hmi_cur_screen;

/* “当前可见页变化”回调：MCU 主动切页(hmi_change_page) 或 屏自行切页(B1 01 应答)
 * 都会触发。app_bp 注册它，在新页落地瞬间立即补刷时钟，避免切页时时钟跳变/各页不同步。
 * 回调里不要再调用 hmi_get_screen()，否则会与屏应答形成查询回环。 */
typedef void (*hmi_page_changed_cb_t)(uint16_t screen_id);
void hmi_set_page_changed_cb(hmi_page_changed_cb_t cb);
void hmi_fire_page_changed(uint16_t screen_id);

/* 底层：发一帧  EE <body...> [CRC] FF FC FF FF （body = 命令+参数） */
void hmi_send_frame(const uint8_t *body, uint16_t body_len);

/* 切换画面: EE B1 00 <画面号:16> */
void hmi_change_page(uint16_t screen_id);

/* 查询当前画面: EE B1 01。屏应答 EE B1 01 <画面号:16> FF FC FF FF
 * —— 这是唯一能自动判定"接线 + 波特率 + 帧格式"三项都正确的指令。 */
void hmi_get_screen(void);

/* 写文本(ASCII/GBK): EE B1 10 <画面:16> <控件:16> <字符串>，字符串自动截断到帧上限 */
void hmi_write_text(uint16_t screen_id, uint16_t ctrl_id, const char *str);

/* 读控件值: EE B1 11 <画面:16> <控件:16>（屏应答控件当前值，可用来确认文本真写进去了） */
void hmi_get_control(uint16_t screen_id, uint16_t ctrl_id);

/* 曲线灌数据: EE B1 32 <画面:16> <控件:16> <通道:8> <点数:16> <每点1字节>，len 超限自动截断 */
void hmi_draw_curve(uint16_t screen_id, uint16_t ctrl_id, uint8_t channel,
                    const uint8_t *data, uint16_t len);

/* 背光 / 蜂鸣（按《大彩指令集 V5.0/V5.1》核实指令码）
 *   hmi_set_backlight(level): level = 0(最亮) .. 255(背光关闭)  —— 【值越大越暗】
 *   hmi_beep(ms)            : 蜂鸣 ms 毫秒（内部换算 10ms 单位，1 字节，超 2550ms 按最大发） */
void hmi_set_backlight(uint8_t level);
void hmi_beep(uint16_t ms);

/* 设置屏内 RTC：EE 81 <秒> <分> <时> <日> <星期> <月> <年>（全部 BCD）
 *
 * 与血氧屏 dc_hmi 的 HMI_SetRTCTime() 同语义、同字段顺序，两块屏可"同一时刻发同一条
 * 指令"，从而实现两屏时钟同源。
 *
 *   year2 : 年份后两位（如 2026 -> 26）
 *   week  : 0=周日 .. 6=周六（血氧侧 rtc_weekday() 也是这个约定）
 *
 * 注意：这不是"写文本"—— 血压屏的时钟控件为 type="rtc"，写文本无效。
 *       设完以后屏内 RTC 自己走，MCU 只需周期性（本项目为每秒）重新校准。 */
void hmi_set_rtc(uint8_t year2, uint8_t month, uint8_t day,
                 uint8_t hour, uint8_t minute, uint8_t second, uint8_t week);

#ifdef __cplusplus
}
#endif
#endif /* HMI_DACAI_H */
