/*!
*  \file meas.h
*  \brief 测量数据源（AFE4490 血氧前端 / 演示数据发生器）
*
*  应用层(app_spo2.c)只依赖本文件给出的 3 个接口 + g_meas 结构，
*  更换传感器或接入真实 AFE4490 时只改 meas.c，不动界面代码。
*
*  两种工作模式（改 meas.c 顶部的 MEAS_USE_AFE4490 宏切换）：
*      0 = 内置演示信号发生器，无需任何硬件即可验证 7 个画面的全部功能
*      1 = 真实 AFE4490（SPI），需自行补上 meas.c 里的 4 个底层函数
*/
#ifndef __MEAS_H
#define __MEAS_H

#include "hmi_user_uart.h"      /* 提供 uint8 / uint16 / int16 / uint32 等类型 */

/* ==================== 测量源配置（meas.c 与 main.c 共用） ==================== */
#define MEAS_USE_AFE4490    0       /* 0 = 演示信号，1 = 真实 AFE4490   */
#define MEAS_DEMO_CYCLE     0       /* 1 = 每 40s 自动切换探头状态      */

/*! 算法版本：算法或数据源变更时递增（合同 8.6 数据字典要求随结果上报）。
*  0x0001 = 内置演示信号发生器；接入真实 AFE4490 算法后从 0x0100 起编。 */
#define MEAS_ALGO_VERSION   0x0001

/*! 统一测量结果，与《STM32F407_AFE4490_M系列串口屏_快速上手开发规划》4.1 节一致 */
typedef struct
{
    uint32  sample_seq;         /* 原始样本序号，每采一点 +1          */
    uint32  timestamp_ms;       /* 该样本的系统时间(ms)               */
    int32   red_raw;            /* 红光原始值（直流分量+交流分量） */
    int32   ir_raw;             /* 红外原始值，波形用它             */
    int16   spo2;               /* 血氧饱和度 %，<0 表示无效         */
    int16   pulse_rate;         /* 脉率 bpm，<0 表示无效             */
    uint8   quality;            /* 灌注质量 0~100                    */
    uint8   probe;              /* 0 未接入 / 1 已接入 / 2 信号差     */
    uint8   valid;              /* 1 本次结果有效                    */
    uint8   fault_code;         /* 故障码，0 = 无                    */
    uint16  algo_version;       /* 算法版本 = MEAS_ALGO_VERSION      */
} measurement_t;

/*! 带序号的原始样本（供日志追溯 / 后续算法滑窗使用） */
typedef struct
{
    uint32  seq;                /* 样本序号，连续编号，断号即丢样     */
    uint32  t_ms;               /* 采样时刻(ms)                       */
    int32   red;                /* 红光原始值                         */
    int32   ir;                 /* 红外原始值                         */
} raw_sample_t;

/* 探头状态取值 */
#define PROBE_OFF       0
#define PROBE_ON        1
#define PROBE_WEAK      2

extern measurement_t g_meas;

/*! 初始化测量源（上电调用一次） */
void Meas_Init(void);

/*! 设定监护运行状态：0 = 暂停（停止出数），1 = 运行 */
void Meas_SetRun(uint8 run);

/*! 周期节拍，建议每 10ms 调用一次；内部按自己的节奏刷新 g_meas */
void Meas_Tick(void);

/*! 取一个新的 PPG 原始采样点，用于绘制实时波形。
*  返回值直接作为波形原始值送进归一化环节；暂停时返回 0。 */
int32 Meas_PpgSample(void);

/*! 从原始样本环形缓冲弹出一个样本（日志/算法消费）。
*  返回 1 = 取到，0 = 缓冲已空。缓冲满时最老的样本被覆盖并计入丢样计数。 */
uint8 Meas_SamplePop(raw_sample_t *out);

/*! 采样环形缓冲累计丢样数（缓冲满被覆盖的样本数，正常应恒为 0） */
uint32 Meas_SampleDrops(void);

#endif /* __MEAS_H */
