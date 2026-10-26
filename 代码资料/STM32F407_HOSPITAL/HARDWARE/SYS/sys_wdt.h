#ifndef __SYS_WDT_H
#define __SYS_WDT_H

#include <stdint.h>

/* 独立看门狗(IWDG) 监督服务
 * 设计目标（见 NodeA 未完成任务清单 §2.2 "看门狗 IWDG"）：
 *   1) 只监督 MCU 主循环是否还"活着并向前推进"，不监督外设数据是否到达。
 *      => 血氧/血压传感器离线、屏断线、串口静默都不会误触发复位（外设离线不误复位）。
 *   2) 主循环每轮到达喂狗点即视为一次有效"前进"，由 main.c 在 while(1) 顶部调用 WDT_Feed()。
 *   3) 若主循环卡死（死循环 / 活锁 / 某关键任务 hang），喂狗停止 -> IWDG 超时复位救回。
 *
 * 注意：绝不在中断里喂狗——若在 SysTick 等中断喂，主循环跑飞也能续命，失去监督意义。
 *
 * LSI 标称 32kHz（手册范围 17~47kHz）。本配置：
 *   prescaler = 128  -> 时钟 = LSI/512
 *   reload    = 625  -> 计数 (625+1) = 626 tick
 *   典型 32kHz: 626 * (512/32000) = 10.0s
 *   最快 47kHz: 626 * (512/47000) = 6.82s  (最坏最小超时，远大于 9600bps 血压屏阻塞发送尖峰)
 *   最慢 17kHz: 626 * (512/17000) = 18.9s
 * 9600bps 下即使一次发 500 字节也仅需 ~0.5s，远小于 6.82s，故不会误咬；真正死锁仍会在 7~19s 内复位。
 */

/* 复位原因（供后续"统一诊断快照" node_status_t 复用） */
#define RESET_REASON_POWER  0u   /* 上电 / 外部复位（POR / PIN） */
#define RESET_REASON_IWDG   1u   /* 看门狗复位（上次跑飞被 IWDG 救回） */
#define RESET_REASON_OTHER  2u   /* 软件复位 / 低功耗 / 窗口看门狗等其它 */

extern uint8_t g_reset_reason;   /* WDT_Init() 中确定，供诊断快照读取 */

void WDT_Init(void);                 /* 读复位标志->记录->配置并启动 IWDG（仅启动后开始计时） */
void WDT_Feed(void);                 /* 喂狗：IWDG_ReloadCounter()。仅由主循环调用 */
const char *WDT_ResetReasonName(void); /* 返回 GBK 字符串的诊断名 */

#endif /* __SYS_WDT_H */
