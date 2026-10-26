/**
 * app_bp.h - 血压子系统对外接口：HKB-08B+ 血压传感器 → 大彩串口屏(血压屏)
 *
 * 硬件通道: 血压屏 USART3@9600, 血压传感器 UART5@115200
 * 依赖: stdint、stm32f4xx(SPL)、merge_cfg.h
 */
#ifndef APP_BP_H
#define APP_BP_H

#include <stdint.h>
#include "stm32f4xx.h"
#include "merge_cfg.h"

/* BP 子系统（血压屏 USART3 + 血压传感器 UART5）的接收方式，0/1 二选一：
 *   0 = 单字节 RXNE 中断（当前双屏并行方案采用）
 *   1 = DMA + 空闲中断(IDLE)（原始单屏工程做法）
 * 两路数据量都很小（屏仅在按键时回几字节；传感器约 10Hz、每帧几字节），
 * 逐字节 RXNE 完全够用，且无需申请 DMA 流、避免与其它外设抢流。
 * stm32f4xx_it.c 用同一宏选择 ISR 分支，故定义在头文件。 */
#ifndef RX_USE_DMA_IDLE
#define RX_USE_DMA_IDLE 0
#endif

/* 血压子系统初始化：装回调、开串口、启动传感器与屏 */
void app_init(void);
/* 血压子系统主循环：周期性轮询 tick、屏可见页与传感器状态，需在 main 主循环反复调用 */
void app_loop(void);

/* 屏在线探测重握手：恢复血压屏显示状态（绝不重新启动测量 0xA0）。
 *   重发时钟 + 状态栏 + 强制回到 MCU 记录的页 + 依页补刷动态内容 + 重发全部历史记录。
 *   作为 screen_watch 的 resync 回调，返回 1（一次性完成）。 */
uint8_t bp_resync(void);

/* 串口接收回调：按 u 区分来源，把单字节喂给传感器解析器或屏应答解析器；
 * 在 USART 接收中断里被调用，须短小、不可阻塞。 */
void on_uart_rx(USART_TypeDef *u, uint8_t b);

/* DMA+空闲中断收尾：把 DMA 暂存区已收字节搬入环形缓冲并重启 DMA。
 * 仅在 RX_USE_DMA_IDLE=1 时使用；当前 =0，不调用。 */
void on_uart_rx_idle(USART_TypeDef *u);

/* 毫秒级 tick 计数，单位 ms，上电起累加；统一复用 SPO2 的 uwTick */
uint32_t get_tick(void);
/* 合并工程：g_tick 别名到 SPO2 sys.c 的 uwTick，避免双 SysTick 各自计数 */
extern volatile uint32_t uwTick;
#define g_tick uwTick

/* ============================================================================
 *  血压测量状态机与最近结果（供 CANopen 上报生产者只读访问）
 *  ----------------------------------------------------------------------------
 *  这两个类型原本定义在 app_bp.c 内部（static），CANopen 上报生产者
 *  (canopen_app.c) 需要读取最近一次血压结果 / 状态机状态，因此提升为对外
 *  类型，并配套 bp_last_get() / bp_state_get() 访问器（调用方只读副本）。
 * ========================================================================== */
/* 测量状态机：空闲 / 测量中 / 停止中 */
typedef enum {
    ST_IDLE = 0,
    ST_MEASURING,
    ST_STOPPING
} bp_state_t;

/* 最近一次血压测量结果（idle 页“上次结果”与结果页的数据源） */
typedef struct {
    uint8_t  valid;            /* 1=有效 0=无结果 */
    uint16_t sys, dia;         /* 收缩/舒张压 mmHg */
    uint8_t  pr;               /* 脉率 bpm */
    uint8_t  irregular;        /* 心律不齐标记：0=齐 1=不齐 */
    uint16_t year;             /* 测量时刻年 */
    uint8_t  mon, day, hour, min;  /* 测量时刻 月/日/时/分 */
    uint32_t valid_tick;       /* 测量成功完成时的 uwTick（D11 时效窗口用；0=未测量） */
} bp_last_t;

/* 读取最近一次血压结果 / 当前状态机状态（CANopen 上报生产者用，调用方只读副本） */
bp_last_t bp_last_get(void);
bp_state_t bp_state_get(void);

#endif /* APP_BP_H */
