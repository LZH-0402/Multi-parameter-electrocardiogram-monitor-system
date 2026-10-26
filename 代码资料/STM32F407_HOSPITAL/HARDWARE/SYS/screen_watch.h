/**
 * screen_watch.h - 屏在线探测与重握手（双屏监护仪通用看屏模块）
 *
 * 背景：单 MCU 带两块串口屏（血氧屏 USART2@115200 / 血压屏 USART3@9600）。
 *   屏可能掉线（排线松、屏复位、供电瞬断）。掉线后 MCU 发出的指令全部石沉大海，
 *   但 MCU 仍在跑、看门狗不会咬；临床上看护仪会出现"屏黑了却没报警"的盲区。
 *
 * 本模块为每块屏维护一个独立状态机，周期性"查询当前画面"(EE B1 01) 探活：
 *   INIT   -> 上电后尚未确认屏在线（连续无回复达到阈值也会升报警，见下）
 *   ONLINE -> 在线
 *   OFFLINE-> 失联：升报警（蜂鸣器），并周期重复蜂鸣
 *   RESYNC -> 失联后收到回复：重握手（重初化显示 + 重发全部历史趋势）
 *
 * 探测参数（由 9600bps 链路推导，详见 .c 注释）：
 *   探测周期   = 1000ms   每 1s 发一次查询
 *   响应超时   = 150ms    发查询后 150ms 内须收到回复
 *   失联阈值   = 3（可调）连续 3 次无回复判失联
 *   离线报警   = 蜂鸣 1000ms，离线期间每 3s 重复
 *
 * 设计要点：
 *   1) 重握手只恢复"显示状态"，绝不重新触发测量（血压屏重握手不得重发 0xA0）。
 *   2) 两屏协议不同（dc_hmi / hmi_dacai），本模块不耦合具体指令，
 *      通过注册 send_probe / beep / resync 回调与 touch() 解耦。
 *   3) 在线标志通过 screen_watch_online() 暴露，供后续 §2.1 node_status_t(CANopen) 复用。
 */
#ifndef SCREEN_WATCH_H
#define SCREEN_WATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 屏 ID（同时是 s_slot 下标） */
#define SCREEN_ID_BP    0u      /* 血压屏（hmi_dacai, USART3@9600） */
#define SCREEN_ID_SPO2  1u      /* 血氧屏（dc_hmi,   USART2@115200） */
#define SCREEN_WATCH_N  2u

/* 状态机 */
typedef enum {
    SW_ST_INIT   = 0,   /* 启动后尚未确认在线（无回复达阈值也会报警） */
    SW_ST_ONLINE = 1,   /* 在线 */
    SW_ST_OFFLINE= 2,   /* 失联：已升报警 */
    SW_ST_RESYNC= 3     /* 收到回复后重握手：重初化显示 + 重发全部历史 */
} sw_state_t;

/* 重握手回调：返回 1=已完成，0=尚未完成（分片进行中，后续 tick 继续调用）。
 *   血压屏一次性完成返回 1；血氧屏趋势历史较长，按时间门控分片返回 0 直到发完。 */
typedef uint8_t  (*sw_resync_cb_t)(void);
typedef void     (*sw_send_probe_cb_t)(void);
typedef void     (*sw_beep_cb_t)(uint16_t ms);

/* 默认参数（详见 screen_watch.c 顶部推导） */
#define SW_PROBE_PERIOD_MS    1000u    /* 探测周期：每 1s 发一次查询 */
#define SW_RESP_TIMEOUT_MS    150u     /* 响应超时：发查询后 150ms 内须收到回复 */
#define SW_MISS_THRESHOLD     3u       /* 失联阈值：连续 3 次无回复判失联（可调） */
#define SW_ALARM_BEEP_MS      1000u    /* 离线报警蜂鸣时长 */
#define SW_OFFLINE_REBEEP_MS  3000u    /* 离线期间每 3s 重复蜂鸣 */

void screen_watch_init(void);
void screen_watch_register(uint8_t id, const char *name,
                           sw_send_probe_cb_t probe, sw_beep_cb_t beep,
                           sw_resync_cb_t resync);
void screen_watch_set_miss_thresh(uint8_t id, uint16_t n);  /* 失联阈值可调 */
void screen_watch_touch(uint8_t id);   /* RX 解析到该屏有效帧（含查询回复）时调用 */
void screen_watch_tick(void);          /* 主循环每轮调用 */
uint8_t screen_watch_online(uint8_t id);          /* 供 node_status_t 复用 */
sw_state_t screen_watch_state(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_WATCH_H */
