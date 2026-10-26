/*!
 * \file node_status.c
 * \brief 统一诊断快照 node_status_t 的实现（NodeA 未完成任务清单 §2.2）
 *
 * 非侵入式：本文件**只读**引用既有全局（g_meas / bp_last / screen_watch /
 * sys_wdt / 各 RX 计数），不修改任何现有逻辑，也不改动任何既有 .c。
 * 整个工程只需新增 node_status.c 到编译（加入 Keil/EIDE 源文件组即可），
 * 原有功能与编译结果不受影响。
 *
 * 用法（均为可选接入，不改也能编过）：
 *   1) 在 main.c 初始化尾部（WDT/screen_watch 之后）调用一次 node_status_init()；
 *   2) 想统一诊断输出时，把心跳里的散装 dbg_puts 换成 node_status_snapshot()+
 *      node_status_dump()；或在 CANopen 生产者里读 node_status_t 写进 2130 位图。
 */

#include "node_status.h"
#include "sys_wdt.h"       /* g_reset_reason / RESET_REASON_* */
#include "screen_watch.h"  /* SCREEN_ID_* / screen_watch_online()/state() */
#include "uno.h"           /* g_meas / g_uno_rx_bytes / g_uno_frames */
#include "app_bp.h"        /* bp_last_get() / bp_state_get() / uwTick */
#include "dc_hmi.h"        /* g_hmi_rx_bytes（血氧屏链路活性计数）*/
#include "debug.h"         /* dbg_* */

/* 模块内部状态（不影响任何外部变量）*/
static uint32_t s_boot_id = 0;
static uint16_t s_q_peak[2] = {0, 0};   /* [0]=UNO 队列, [1]=HMI 队列 */

void node_status_init(void)
{
    /* 每次上电自增。注意：调用方应确保在 WDT_Init() 之后，
     * 令本模块快照到的 g_reset_reason 已确定。 */
    s_boot_id++;
}

void node_status_q_peak_update(uint8_t ch, uint16_t cur_depth)
{
    if (ch < 2 && cur_depth > s_q_peak[ch]) {
        s_q_peak[ch] = cur_depth;
    }
}

void node_status_snapshot(node_status_t *st)
{
    if (!st) return;

    st->boot_id     = s_boot_id;
    st->uptime_sec  = uwTick / 1000u;
    st->reset_reason = g_reset_reason;

    /* 在线位图：屏在线走 screen_watch 探活；探头/传感器走既有状态。
     * UNO/HKB 用"历史上曾成功"做存在性代理，避免引入新计数器。 */
    uint8_t on = 0;
    if (screen_watch_online(SCREEN_ID_SPO2)) on |= NS_ON_SPO2;
    if (screen_watch_online(SCREEN_ID_BP))   on |= NS_ON_BP;
    if (g_meas.probe_state != 0u)            on |= NS_ON_PROBE;
    if (g_uno_frames    != 0u)               on |= NS_ON_UNO;

    bp_last_t last = bp_last_get();
    if (last.valid)                          on |= NS_ON_HKB;
    st->online = on;

    st->screen_spo2_state = (uint8_t)screen_watch_state(SCREEN_ID_SPO2);
    st->screen_bp_state   = (uint8_t)screen_watch_state(SCREEN_ID_BP);

    st->uno_rx_bytes = g_uno_rx_bytes;
    st->hmi_rx_bytes = g_hmi_rx_bytes;
    st->uno_frames   = g_uno_frames;

    st->q_peak_uno = s_q_peak[0];
    st->q_peak_hmi = s_q_peak[1];

    st->spo2_valid  = (uint8_t)(g_meas.result_valid && (g_meas.spo2 != 0));
    st->pr_valid    = (g_meas.pulse_rate > 0) ? 1u : 0u;
    st->probe_state = g_meas.probe_state;
    st->bp_valid    = last.valid;
    st->bp_state    = (uint8_t)bp_state_get();
}

void node_status_dump(const node_status_t *st)
{
    if (!st) return;
    dbg_puts("[NODE] boot=");   dbg_put_u32(st->boot_id);
    dbg_puts(" up=");           dbg_put_u32(st->uptime_sec); dbg_puts("s");
    dbg_puts(" rst=");          dbg_puts(WDT_ResetReasonName());
    dbg_puts(" on=0x");         dbg_put_hex8(st->online);
    dbg_puts(" qpk=");          dbg_put_u32(st->q_peak_uno);
    dbg_puts("/");              dbg_put_u32(st->q_peak_hmi);
    dbg_puts(" spo2v=");        dbg_put_u32(st->spo2_valid);
    dbg_puts(" prv=");          dbg_put_u32(st->pr_valid);
    dbg_puts(" bpv=");          dbg_put_u32(st->bp_valid);
    dbg_puts(" bps=");          dbg_put_u32(st->bp_state);
    dbg_puts("\r\n");
}
