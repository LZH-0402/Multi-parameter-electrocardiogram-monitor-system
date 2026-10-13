#ifndef MERGE_CFG_H
#define MERGE_CFG_H

/*
 * merge_cfg.h — STM32F407_HOSPITAL 合并工程 编译期配置
 * ============================================================================
 * 双屏双传感器并行（血氧屏 + 血压屏 同时工作）：
 *   USART1(PA9/PA10)  调试口    115200   debug.c
 *   USART2(PA2/PA3)   血氧屏    115200   dc_hmi 驱动（P3 → SP3232 通道1 → COM_M）
 *   USART3(PB10/PB11) 血压屏    9600     hmi_dacai 驱动（P5 → SP3232 通道2 → COM_G）
 *   UART5 (PC12/PD2)  血压传感器 115200  HKB-08B+（TTL）
 *   USART6(PC6/PC7)   血氧传感器 57600   UNO(AFE4490)
 *
 * 说明：
 *   - 两块屏占用两个不同的 UART（USART2 / USART3），可同时连接、同时刷新，不再二选一。
 *   - 血压传感器从原 USART3 挪到 UART5(PC12/PD2)，从而把 USART3 让给第二块血压屏。
 *   - 两屏波特率须与各自屏工程的 DeviceBaudRate 一致（血氧 115200 / 血压 9600）。
 *   - 原先的 HMI_SCREEN_SPO2 / HMI_SCREEN_BP 二选一宏已取消（双屏并行不再需要）。
 * ============================================================================ */

#include <stdint.h>

/*
 * ★ 两块屏共用的软 RTC 基准（uwTick 毫秒）。
 * ----------------------------------------------------------------------------
 * 两个子系统各自维护一份"软 RTC"（血氧：app_bloodoxygen.c 的 s_year/s_mon/
 * s_day + s_rtc_last_tick；血压：app_bp.c 的 s_year/s_month/s_day +
 * s_rtc_base_tick），但**必须从同一个 tick 起算**，否则两块屏显示的时间会
 * 永久差一个常数秒：
 *
 *   app_hmi_init()（血氧侧）在 main() 第 43 行调用，要刷几十帧屏指令（报警记录
 *   清空 / 背光 / 趋势 / 各页探头状态 / 屏 RTC），耗时约数百毫秒~2 秒；
 *   app_init()（血压侧）在 main() 第 51 行调用，其 s_rtc_base_tick 在函数体
 *   第 1099 行才赋值。于是血压软 RTC 的"0 秒"比血氧侧晚了 Δ，
 *   表现为"血压屏时间稳定比血氧屏慢 Δ 秒"（实测约 2 秒）。
 *
 * 处理：谁先初始化谁把基准写进来，后初始化的一方直接复用同一个 tick。
 * 这样两份软 RTC 在任意时刻 tick 相同 -> floor((tick-base)/1000) 完全相同。
 */
extern uint32_t g_rtc_epoch_tick;

#endif /* MERGE_CFG_H */
