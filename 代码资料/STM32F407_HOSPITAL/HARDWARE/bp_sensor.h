/**
 * bp_sensor.h - HKB-08B+ 血压传感器 UART 协议（组帧 + 流式解析）
 *
 * 协议（已按官方 PDF V2.2 核对）：
 *   帧: 0xFF 0xC0 | LEN | CKSUM | CMD | DATA...
 *   LEN    = 3 + N(DATA)                      // 含 LEN 自身、CKSUM、CMD、DATA 的总字节数
 *   CKSUM  = (LEN + CMD + sum(DATA)) & 0xFF   // 低 8 位，含 LEN
 *   特例: 复位指令 0xFF 0x00（无帧头，仅两字节）
 *
 * 本文件不依赖任何 MCU SDK，纯 C，方便移植到 STM32/ESP32/PC。
 * 在 UART 接收中断里调用 bp_sensor_feed() 逐字节喂入即可。
 */
#ifndef BP_SENSOR_H
#define BP_SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 命令字 ---- */
#define BP_CMD_WAKE       0xAA  /* 唤醒 */
#define BP_CMD_SLEEP      0xA8  /* 休眠 */
#define BP_CMD_START      0xA0  /* 启动测量 */
#define BP_CMD_STOP       0xA1  /* 停止测量 */
#define BP_CMD_START_WAVE 0xD0  /* 启动静压测波形 */
#define BP_CMD_STOP_WAVE  0xD1  /* 停止静压测波形 */
#define BP_CMD_SET_GAIN   0xD2  /* 设置放大倍数(1~7) */
#define BP_CMD_WAVE       0xD3  /* 波形数据点(200Hz) */
#define BP_CMD_RESULT     0xAC  /* 测量结果 */
#define BP_CMD_ERROR      0xAD  /* 错误报告 */

/* ---- 错误码(0xAD 的 X) ---- */
typedef enum {
    BP_ERR_NO_PULSE   = 0,  /* 测不到有效脉搏 */
    BP_ERR_NO_PRESS   = 1,  /* 11s 内打气不上 50mmHg(气袋没绑好) */
    BP_ERR_BAD_VALUE  = 2,  /* 测量结果数值有误 */
    BP_ERR_OVERPRESS  = 3,  /* 气袋压力超 295mmHg，超压保护 */
    BP_ERR_TOO_MUCH   = 4   /* 干预过多(测量中移动/说话等) */
} bp_error_t;

/* ---- 解析出的测量结果 ---- */
typedef struct {
    uint16_t systolic;   /* 收缩压 mmHg */
    uint16_t diastolic;  /* 舒张压 mmHg */
    uint8_t  heart_rate; /* 心率 bpm */
    uint8_t  irregular;  /* 1=心率不齐(SSYH.7) */
} bp_result_t;

/* ---- 实时气压(测量中,约10Hz) ---- */
typedef struct {
    uint16_t pressure;   /* 当前气压 mmHg */
    uint8_t  has_beat;   /* 1=有心跳(QYH.4) */
} bp_pressure_t;

/* ---- 波形点(静压模式 200Hz) ---- */
typedef struct {
    uint16_t value;      /* waveH<<8 | waveL */
} bp_wave_t;

/* 回调：每解出一帧调用一次。cmd 为命令字，data 为 DATA 部分，len 为 DATA 长度 */
typedef void (*bp_frame_cb_t)(uint8_t cmd, const uint8_t *data, uint8_t len);

/* ---- API ---- */
void bp_sensor_init(bp_frame_cb_t cb);
void bp_sensor_feed(uint8_t b);                 /* 逐字节喂入解析器(中断安全) */
void bp_build_cmd(uint8_t *out, uint8_t *out_len,
                  uint8_t cmd, const uint8_t *data, uint8_t data_len); /* 组帧 */

/* 便捷组帧：返回帧长度，buf 需 >= 数据+6 */
uint8_t bp_make_cmd(uint8_t *buf, uint8_t cmd,
                    const uint8_t *data, uint8_t data_len);

/* 解析辅助 */
void bp_parse_result(const uint8_t *d, bp_result_t *r);
void bp_parse_pressure(const uint8_t *d, bp_pressure_t *p);
void bp_parse_wave(const uint8_t *d, bp_wave_t *w);

#ifdef __cplusplus
}
#endif
#endif /* BP_SENSOR_H */
