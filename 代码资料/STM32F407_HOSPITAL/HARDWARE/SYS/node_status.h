#ifndef __NODE_STATUS_H
#define __NODE_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 统一诊断快照 node_status_t（NodeA 未完成任务清单 §2.2）
 * ------------------------------------------------------------
 * 把分散在 g_meas / bp_last / screen_watch / sys_wdt / 各 RX 计数 的健康状态
 * 聚合成一份结构，供 屏(HMI) / 调试日志(USART1) / CANopen 三处共用同一口径。
 *
 * 非侵入式设计：本模块**只读**引用既有全局（extern），不修改任何现有逻辑，
 * 不改动任何既有 .c。新增 node_status.c/.h 两个文件即可，原工程行为零变化。
 * ============================================================ */

/* 在线标志位图（node_status_t.online 的 bit 定义）*/
#define NS_ON_SPO2  (1u << 0)  /* 血氧屏在线（screen_watch 探活）*/
#define NS_ON_BP    (1u << 1)  /* 血压屏在线（screen_watch 探活）*/
#define NS_ON_PROBE (1u << 2)  /* 血氧探头已接入（g_meas.probe_state != 0）*/
#define NS_ON_UNO   (1u << 3)  /* UNO(AFE4490) 历史上产出过有效帧（链路存在）*/
#define NS_ON_HKB   (1u << 4)  /* 血压传感器最近一次测量有效（结果存在）*/

typedef struct {
    /* 身份与寿命 */
    uint32_t boot_id;        /* 启动代次：每次上电 node_status_init() 自增（与 CANopen 2130:02 同源思路）*/
    uint32_t uptime_sec;     /* 运行秒数 = uwTick / 1000 */
    uint8_t  reset_reason;   /* 复位原因：RESET_REASON_POWER / IWDG / OTHER（见 sys_wdt.h）*/

    /* 在线标志与屏状态机 */
    uint8_t  online;             /* NS_ON_* 位图 */
    uint8_t  screen_spo2_state;  /* 血氧屏 screen_watch 状态机（sw_state_t）*/
    uint8_t  screen_bp_state;    /* 血压屏 screen_watch 状态机 */

    /* 链路活性计数（证明"在通信"，非峰值）*/
    uint32_t uno_rx_bytes;   /* UNO(AFE4490) 收到字节总数 */
    uint32_t hmi_rx_bytes;   /* 血氧屏 收到字节总数 */
    uint32_t uno_frames;     /* UNO 成功解析帧数 */

    /* 队列高峰水位（需调用 node_status_q_peak_update 才更新；不调用恒为 0，不影响功能）*/
    uint16_t q_peak_uno;     /* UNO RX 队列历史高峰 */
    uint16_t q_peak_hmi;     /* 血氧屏 RX 队列历史高峰 */

    /* 业务有效性摘要（只读引用既有数据，不另存副本）*/
    uint8_t  spo2_valid;     /* SpO2 数值有效 */
    uint8_t  pr_valid;       /* 脉率有效 */
    uint8_t  probe_state;    /* 探头状态（0 未接 / 1 接）*/
    uint8_t  bp_valid;       /* 最近一次血压结果有效 */
    uint8_t  bp_state;       /* 血压测量状态机（bp_state_t）*/
} node_status_t;

/* 上电调用一次：boot_id 自增。须确保 g_reset_reason 已定（放在 WDT/screen_watch 初始化之后）。*/
void node_status_init(void);

/* 抓取当前快照到 st（只读取既有全局，绝不改写）*/
void node_status_snapshot(node_status_t *st);

/* 打印快照到调试串口 USART1（dbg_*）*/
void node_status_dump(const node_status_t *st);

/* 可选：在 RX 中断/任务里调用以更新队列高峰水位；不调用则 q_peak 恒为 0。
 *   ch: 0 = UNO 队列, 1 = HMI 队列；cur_depth = 当前队列占用。*/
void node_status_q_peak_update(uint8_t ch, uint16_t cur_depth);

#ifdef __cplusplus
}
#endif

#endif /* __NODE_STATUS_H */
