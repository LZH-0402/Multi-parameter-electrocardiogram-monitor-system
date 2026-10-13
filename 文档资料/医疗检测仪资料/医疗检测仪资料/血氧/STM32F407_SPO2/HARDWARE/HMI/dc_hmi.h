/*!
 * \file dc_hmi.h
 * \brief 大彩串口屏（DC80480DN050 / SPO2_Hospital）下行驱动 + 上行通知解析
 *        —— 已移植为标准库 SPL 版本，使用 USART2（PA2/PA3）。
 *
 * 帧格式（本工程 DeviceEnableCRC="0"，不启用 CRC）：
 *      下行：EE <命令和参数> FF FC FF FF
 *      长度 16/32 位参数：高字节在前
 *
 * 指令字节来源：M 系列出厂工程配套单片机程序的 hmi_driver.c，并经本机
 * VisualTFT 的 InstructConfig.xml 交叉核对。曲线/历史曲线/批量更新类子指令
 * 不在该精简指令表内，首次接入请用虚拟串口屏实测确认。
 *
 * 硬件：USART2 PA2(TX)/PA3(RX)，波特率与屏端保持一致（115200）。
 *      DC80480DN050 出厂默认 RS232 电平，接 TTL 前必须短接 J5（或用 SP3232 转 RS232）。
 */

#ifndef __DC_HMI_H
#define __DC_HMI_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 移植配置项 ---------------- */

/* 每帧最大长度（曲线批量帧要够大：点数 + 16 字节开销） */
#ifndef HMI_TX_BUF_SIZE
#define HMI_TX_BUF_SIZE     320
#endif

/* 曲线批量发送的点数：一次帧里塞多少个点。建议 8~32 */
#ifndef HMI_WAVE_CHUNK
#define HMI_WAVE_CHUNK      16
#endif

/* ---------------- 底层发送（由 usart.c 用 USART2 实现） ---------------- */
void hmi_tx(const uint8_t *p, uint16_t n);

/* ---------------- 初始化 ---------------- */
void HMI_Init(void);

/* ---------------- 下行：控件写入 ---------------- */

void HMI_SetScreen(uint16_t screen_id);
void HMI_GetScreen(void);   /* 查询当前画面: EE B1 01，屏会应答 EE B1 01 <画面:16> FF FC FF FF */
void HMI_SetText(uint16_t screen_id, uint16_t ctrl_id, const char *str);
void HMI_SetTextInt(uint16_t screen_id, uint16_t ctrl_id, int32_t value,
                    uint8_t digits);          /* 依赖固件 >= 908 */
void HMI_SetProgress(uint16_t screen_id, uint16_t ctrl_id, uint32_t value);
void HMI_SetButton(uint16_t screen_id, uint16_t ctrl_id, uint8_t state);

/*! 运行时切换控件显隐 —— 指令集 EE B1 03 <screen><ctrl><visible>
 *  visible: 1=显示 / 0=隐藏（不是"可见"的反义 0="不可见"）
 *  ★ 本工程用它来做"暂停监护 / 继续监护"两个同位置按钮的切换：
 *    暂停时隐藏 btn_pause(1)、显示 btn_resume(6)，恢复时反之。
 *  注意：切换画面后屏会按控件默认属性重绘，需在进入画面时重新下发一次。 */
void HMI_SetControlVisible(uint16_t screen_id, uint16_t ctrl_id, uint8_t visible);

/*! 运行时切换控件使能（灰化/不可点） —— 指令集 EE B1 04 <screen><ctrl><enable> */
void HMI_SetControlEnable(uint16_t screen_id, uint16_t ctrl_id, uint8_t enable);

/* ---------------- 下行：曲线控件 graph ---------------- */
void HMI_GraphClear(uint16_t screen_id, uint16_t ctrl_id, uint8_t channel);
void HMI_GraphAddData(uint16_t screen_id, uint16_t ctrl_id, uint8_t channel,
                      const uint8_t *data, uint16_t len);

/* ---------------- 下行：历史曲线 historygraph ---------------- */
void HMI_HistoryEnable(uint16_t screen_id, uint16_t ctrl_id, uint8_t enable);
void HMI_HistoryTimeLength(uint16_t screen_id, uint16_t ctrl_id, uint16_t count);
void HMI_HistorySampleU16(uint16_t screen_id, uint16_t ctrl_id,
                          const uint16_t *value, uint8_t channel_cnt);

/* ---------------- 下行：批量更新（一条帧更新多个控件，省带宽） ---------------- */
void HMI_BatchBegin(uint16_t screen_id);
void HMI_BatchSetText(uint16_t ctrl_id, const char *str);
void HMI_BatchSetProgress(uint16_t ctrl_id, uint32_t value);
void HMI_BatchEnd(void);

/* 屏幕刷新开关：批量更新前后成对调用，可消除闪烁 */
void HMI_ScreenUpdateEnable(uint8_t enable);

/* ---------------- 下行：系统（RTC 时间 / 背光 / 蜂鸣器） ---------------- */

/*! 设置屏内部 RTC —— 指令集 V5.1 第 2.38 节
 *    EE 81 <Sec> <Min> <Hour> <Day> <Week> <Mon> <Year> FF FC FF FF
 *  year2 传 2 位年（26 = 2026）；week 传 0=周日 .. 6=周六；内部自动转 BCD。 */
void HMI_SetRTCTime(uint8_t year2, uint8_t mon, uint8_t day,
                    uint8_t hour, uint8_t min, uint8_t sec, uint8_t week);

/*! 读取屏内部 RTC —— 指令集 V5.1 第 2.39 节：EE 82 FF FC FF FF
 *  屏会回 EE F7 Year Mon Week Day Hour Min Sec FF FC FF FF，经 NotifyRTC() 上报。 */
void HMI_ReadRTC(void);

/*! 背光调节 —— EE 60 <level>，level【0=最亮 .. 255=最暗】（注意是"越大越暗"） */
void HMI_SetBackLight(uint8_t level);

/*! 蜂鸣器 —— EE 61 <n>，n 单位为 10ms（n=5 -> 响 50ms） */
void HMI_Beep(uint16_t ms);

/* ---------------- 应用层：波形 ---------------- */

/*! 推入一个 PPG 原始采样，内部完成 0~255 归一化并攒够 HMI_WAVE_CHUNK 点后自动发帧。
 *  归一化采用滑动窗口极值，窗口长度 = 曲线控件 sample_count（300）。
 */
void HMI_WavePush(int32_t ir_raw);
void HMI_WaveFlush(void);

/* ---------------- 上行：通知解析 ---------------- */

/*! 串口中断里收到一个字节就调用它；不要在中断里做解析 */
void HMI_RxByte(uint8_t b);

/*! 自屏(USART2 RX)累计收到的字节数：>0 说明屏有回数据（可用于判定链路/波特率） */
extern volatile uint32_t g_hmi_rx_bytes;

/*! 在主循环/任务里周期调用，从上行队列取帧并回调下面的 Notify* */
void HMI_Poll(void);

/* 以下回调由应用层实现（本文件提供弱定义，避免未实现时链接报错） */
void NotifyScreen(uint16_t screen_id);
void NotifyButton(uint16_t screen_id, uint16_t ctrl_id, uint8_t state);
void NotifyProgress(uint16_t screen_id, uint16_t ctrl_id, uint32_t value);
void NotifySlider(uint16_t screen_id, uint16_t ctrl_id, uint32_t value);
void NotifyText(uint16_t screen_id, uint16_t ctrl_id, const uint8_t *str, uint16_t len);

/*! 屏 RTC 读数上报（由 HMI_Poll 解析 EE F7 帧后调用）。年/月/日/时/分/秒均为十进制。 */
void NotifyRTC(uint8_t year2, uint8_t mon, uint8_t day,
               uint8_t hour, uint8_t min, uint8_t sec, uint8_t week);

#ifdef __cplusplus
}
#endif

#endif /* __DC_HMI_H */
