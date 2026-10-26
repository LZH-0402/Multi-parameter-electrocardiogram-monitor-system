/**
 * screen_watch.c - 屏在线探测与重握手实现（见 screen_watch.h 头注释）
 *
 * 探测参数推导（按 9600bps 最慢链路，即血压屏）：
 *   单帧查询下行 = EE B1 01 + [CRC2] + FF FC FF FF = 9 字节
 *   单帧查询上行 = EE B1 01 <page:16> + [CRC2] + FF FC FF FF = 11 字节
 *   9600bps 下纯线路时间 ≈ (9+11)*10/9600 ≈ 20.8ms；屏解析 + 回包通常在数十 ms 内。
 *   => 取响应超时 150ms（约 7 倍线路时间，留足屏处理余量）足够稳妥。
 *   探测周期取 1000ms：既保证"秒级"探活，又让 9600 链路单轮只占 ~2%，
 *   不挤占血氧 PPG 波形(115200)与传感器帧。
 *   失联判定时延 ≈ 阈值*周期 + 超时 = 3*1000 + 150 ≈ 3.15s（满足 ≤3s 量级的临床要求）。
 *
 * 依赖：sys.h 的 millis()（= uwTick，两屏共用同一 SysTick 计数）。
 */
#include "screen_watch.h"
#include "sys.h"     /* millis() */

typedef struct {
    uint8_t  id;
    const char *name;          /* 调试打印用（ASCII） */
    sw_state_t state;
    uint32_t last_resp_tick;   /* 最近一次收到该屏有效帧的时刻 */
    uint32_t probe_sent_tick;  /* 最近一次发出查询的时刻 */
    uint32_t next_probe_tick;  /* 下一次应发查询的时刻（首轮 = 0 立即发） */
    uint32_t offline_beep_tick;/* 离线期间上次蜂鸣时刻 */
    uint16_t miss;             /* 连续无回复计数 */
    uint16_t miss_thresh;      /* 失联阈值（可调） */
    uint8_t  probe_outstanding;/* 1 = 已发查询、待回复 */
    uint8_t  online_flag;      /* 供 node_status_t 复用 */
    sw_send_probe_cb_t send_probe;
    sw_beep_cb_t        beep;
    sw_resync_cb_t      resync;
} sw_slot_t;

static sw_slot_t s_slot[SCREEN_WATCH_N];

/* ---------------------------------------------------------------- */
void screen_watch_init(void)
{
    uint8_t i;
    for (i = 0u; i < SCREEN_WATCH_N; i++) {
        sw_slot_t *s = &s_slot[i];
        s->id = i;
        s->name = (i == SCREEN_ID_BP) ? "BP" : "SPO2";
        s->state = SW_ST_INIT;
        s->last_resp_tick = 0u;
        s->probe_sent_tick = 0u;
        s->next_probe_tick = 0u;     /* 首轮主循环即发第一次查询 */
        s->offline_beep_tick = 0u;
        s->miss = 0u;
        s->miss_thresh = SW_MISS_THRESHOLD;
        s->probe_outstanding = 0u;
        s->online_flag = 0u;
        s->send_probe = 0;
        s->beep = 0;
        s->resync = 0;
    }
}

void screen_watch_register(uint8_t id, const char *name,
                           sw_send_probe_cb_t probe, sw_beep_cb_t beep,
                           sw_resync_cb_t resync)
{
    if (id >= SCREEN_WATCH_N) return;
    s_slot[id].name = name;
    s_slot[id].send_probe = probe;
    s_slot[id].beep = beep;
    s_slot[id].resync = resync;
}

void screen_watch_set_miss_thresh(uint8_t id, uint16_t n)
{
    if (id >= SCREEN_WATCH_N) return;
    if (n < 1u) n = 1u;
    s_slot[id].miss_thresh = n;
}

/* RX 解析到该屏任一有效帧（含对查询 EE B1 01 的回复）时调用 —— 证明屏还活着 */
void screen_watch_touch(uint8_t id)
{
    if (id >= SCREEN_WATCH_N) return;
    s_slot[id].last_resp_tick = millis();
}

uint8_t screen_watch_online(uint8_t id)
{
    if (id >= SCREEN_WATCH_N) return 0u;
    return s_slot[id].online_flag;
}

sw_state_t screen_watch_state(uint8_t id)
{
    if (id >= SCREEN_WATCH_N) return SW_ST_INIT;
    return s_slot[id].state;
}

/* ---------------------------------------------------------------- */
void screen_watch_tick(void)
{
    uint32_t now = millis();
    uint8_t  i;

    for (i = 0u; i < SCREEN_WATCH_N; i++) {
        sw_slot_t *s = &s_slot[i];
        if (s->send_probe == 0) continue;   /* 未注册的屏跳过 */

        /* (1) 探测节拍：到点发查询，并标记本轮"待回复" */
        if ((int32_t)(now - s->next_probe_tick) >= 0) {
            s->send_probe();
            s->probe_sent_tick = now;
            s->next_probe_tick = now + (uint32_t)SW_PROBE_PERIOD_MS;
            s->probe_outstanding = 1u;
        }

        /* (2) 评估本轮查询是否得到回复 */
        if (s->probe_outstanding) {
            uint32_t since_resp  = now - s->last_resp_tick;
            uint32_t since_probe = now - s->probe_sent_tick;

            if (since_resp <= (uint32_t)SW_RESP_TIMEOUT_MS) {
                /* 超时窗口内收到过该屏任意有效帧 => 视为在线 */
                s->probe_outstanding = 0u;
                s->miss = 0u;
                if (s->state == SW_ST_OFFLINE) {
                    /* 失联后恢复：进入重握手，先把在线标志拉起 */
                    s->state = SW_ST_RESYNC;
                    s->online_flag = 1u;
                } else if (s->state == SW_ST_INIT) {
                    s->state = SW_ST_ONLINE;
                    s->online_flag = 1u;
                }
                /* 已是 ONLINE/RESYNC：无需动作 */
            } else if (since_probe > (uint32_t)SW_RESP_TIMEOUT_MS) {
                /* 查询超时：本轮无回复 */
                s->probe_outstanding = 0u;
                if (s->miss < 0xFFFFu) s->miss++;
                if (s->miss >= s->miss_thresh && s->state != SW_ST_OFFLINE) {
                    /* 连续无回复达阈值 => 判失联，升报警 */
                    s->state = SW_ST_OFFLINE;
                    s->online_flag = 0u;
                    s->offline_beep_tick = now;
                    if (s->beep) s->beep(SW_ALARM_BEEP_MS);
                }
            }
        }

        /* (3) 重握手：重初化显示 + 重发全部历史（可跨多个 tick 分片完成） */
        if (s->state == SW_ST_RESYNC && s->resync) {
            uint8_t done = s->resync();
            if (done) {
                s->state = SW_ST_ONLINE;
                s->online_flag = 1u;
                s->miss = 0u;
            }
        }

        /* (4) 离线期间周期重复蜂鸣，保持报警醒目 */
        if (s->state == SW_ST_OFFLINE) {
            if ((int32_t)(now - s->offline_beep_tick) >= (int32_t)SW_OFFLINE_REBEEP_MS) {
                s->offline_beep_tick = now;
                if (s->beep) s->beep(SW_ALARM_BEEP_MS);
            }
        }
    }
}
