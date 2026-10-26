/**
 * canopen_app.h - 监护仪 Node-A CANopen 应用封装层（裸机 / SPL / Keil AC5）
 *
 * 职责：把 CANopenNode v4 协议栈与应用层（双屏监护仪）粘合起来，实现
 *   §2.1 "CANopen 上报 Gateway-C"：
 *     - 协议栈初始化（CO_new -> CO_CANinit -> CO_CANopenInit(上电自主 OPERATIONAL)
 *       -> CO_CANopenInitPDO -> CO_CANsetNormalMode）；
 *     - 1ms 时基驱动 CO_process / CO_process_SYNC / CO_process_RPDO / CO_process_TPDO；
 *     - 生产者：把最新生理参数写进私有 OD(2100~2130 + 2110 波形)，由 5 路 TPDO 周期上报；
 *     - CAN 中断统一入口封装（供 stm32f4xx_it.c 的三路 CAN 向量调用）；
 *     - 故障 EMCY 便捷封装。
 *
 * 依赖：CANopen.h（CO_t / 各 process 函数）、OD.h（OD_RAM 变量）、
 *       app_bp.h（bp_last_get / bp_state_get）、uno.h（g_meas）、sys.h（uwTick）。
 *
 * 设计要点：
 *   - 启用 CO_USE_GLOBALS，CO_new(NULL,...) 返回全局静态对象，免 heap；
 *   - Node-A 固定 500kbps、Node-ID=1、上电即自主进入 OPERATIONAL（无需 NMT 主机）；
 *   - 波形（PPG/ECG）按既定方案"后补"，本版仅上报标量 + 状态。
 */
#ifndef CANOPEN_APP_H
#define CANOPEN_APP_H

#include <stdint.h>
#include "CANopen.h"   /* CO_t, CO_process, CO_process_*, CO_NMT_STARTUP_TO_OPERATIONAL 等 */

/* Node-A 节点 ID（CANopen 1..127；与 Gateway-C 约定一致） */
#ifndef CANOPEN_NODE_ID
#define CANOPEN_NODE_ID  1u
#endif
/* CAN 比特率（名义 kbps）：监护仪 CAN 协议固定 500k。
 * 注：bxCAN 驱动(CO_driver.c)内部已硬编码 500kbps(BTR 写死)，CO_CANinit 的
 *     uint16_t bitRate 参数在本工程被忽略；此处取 500 仅占位（uint16 上限 65535，
 *     不足以表达 500000bps，直接用 500000 会触发编译器截断警告）。 */
#define CANOPEN_BITRATE  500u

/* 通信重置 / 初始化：在 main 外设初始化阶段调用一次。
 * 内部依次 CO_new -> CO_CANinit -> CO_CANopenInit(STARTUP_TO_OPERATIONAL)
 *              -> CO_CANopenInitPDO -> CO_CANsetNormalMode，并递增 boot_generation。
 * @return 0 成功；非 0 = CO_ReturnError_t 错误码（见 301/CO_driver.h）。 */
uint8_t canopen_app_init(void);

/* 主循环周期处理：在 main 的 while(1) 内每轮调用一次（建议放在业务任务之后）。
 * 内部以 uwTick 自算时间差(us)，调用 CO_process + CO_process_SYNC/RPDO/TPDO，
 * 并在通信正常时调用 canopen_app_produce() 刷新 TPDO 数据源。
 * 若 CO_process 返回通信/应用重置请求，会自动重跑初始化（应用层无需干预）。 */
void canopen_app_process(void);

/* 生产者：把应用层最新生理参数写入 OD RAM 区(2100~2130 + 2110 波形)供 TPDO 周期上报。
 * 幂等、非阻塞、无锁（裸机下与 TPDO 同在主循环上下文，无并发）。 */
void canopen_app_produce(void);

/* CAN 中断统一入口：供 stm32f4xx_it.c 的 CAN1_RX0/TX/SCE 三个向量调用。
 * 内部转调 CO_CANinterrupt(&g_CO->CANmodule)。 */
void canopen_app_CAN_interrupt(void);

/* EMCY 紧急上报便捷封装：errCode=厂商紧急错误码，errReg=错误寄存器位，subErr=附加信息。
 * 仅在本模块检测到致命故障（如致命传感器错误）时调用；总线类错误由协议栈自行上报。 */
void canopen_app_emcy(uint16_t errCode, uint8_t errReg, uint8_t subErr);

#endif /* CANOPEN_APP_H */
