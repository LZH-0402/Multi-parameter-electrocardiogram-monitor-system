/*
 * Device and application specific definitions for CANopenNode.
 *
 * @file        CO_driver_target.h
 * @author      Janez Paternoster (original) / 双屏监护仪 Node-A 移植
 * @copyright   2021 Janez Paternoster
 *
 * This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.
 *
 * ----------------------------------------------------------------------------
 *  目标平台：STM32F407ZGT6 + 标准外设库(SPL) + Keil MDK / armcc(AC5)
 *  裸机（无 RTOS），使用 CANopenNode v4 的 CO_USE_GLOBALS 静态对象模式（免 heap）。
 * ----------------------------------------------------------------------------
 */

#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

/* This file contains device and application specific definitions. It is included from CO_driver.h, which contains
 * documentation for common definitions below. */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef CO_DRIVER_CUSTOM
#include "CO_driver_custom.h"
#endif

/* ============================================================================
 *  CANopenNode 协议栈配置（CO_CONFIG_*）
 *  ----------------------------------------------------------------------------
 *  CO_config.h 仅在 DOXYGEN 下给出默认 CO_CONFIG_*；非 DOXYGEN 构建中，顶层宏
 *  需由用户在 CO_driver_target.h 中定义（子标志位由 CO_config.h 无条件定义，可引用）。
 *  未列出的顶层宏保持“未定义” = 0 = 关闭（AC5 的 #if 中未定义标识符按 0 处理）。
 *
 *  本工程（监护仪 Node-A）使能：NMT(状态回调) / 心跳消费者 / 紧急生产者(历史) /
 *  SDO 服务器(分段) / TIME / SYNC / PDO(Rx+Tx+定时器+同步+OD IO 访问) / LED。
 *  关闭：NMT 主机 / 节点守护 / SDO 客户端 / LSS / 存储 / 网关 / SRDO / TRACE / GFC。
 * ========================================================================== */
#define CO_USE_GLOBALS            /* 使用静态全局对象，避免裸机 heap 依赖 */

#define CO_CONFIG_NMT       (CO_CONFIG_NMT_CALLBACK_CHANGE)
#define CO_CONFIG_HB_CONS   (CO_CONFIG_HB_CONS_ENABLE)
#define CO_CONFIG_EM        (CO_CONFIG_EM_PRODUCER | CO_CONFIG_EM_PROD_CONFIGURABLE | CO_CONFIG_EM_HISTORY)
#define CO_CONFIG_SDO_SRV   (CO_CONFIG_SDO_SRV_SEGMENTED)
/* CO_CONFIG_SDO_CLI  不定义 -> SDO 客户端关闭 */
#define CO_CONFIG_TIME      (CO_CONFIG_TIME_ENABLE)
#define CO_CONFIG_SYNC      (CO_CONFIG_SYNC_ENABLE)
#define CO_CONFIG_PDO       (CO_CONFIG_RPDO_ENABLE | CO_CONFIG_TPDO_ENABLE            \
                             | CO_CONFIG_RPDO_TIMERS_ENABLE | CO_CONFIG_TPDO_TIMERS_ENABLE \
                             | CO_CONFIG_PDO_SYNC_ENABLE | CO_CONFIG_PDO_OD_IO_ACCESS)
#define CO_CONFIG_LEDS      (CO_CONFIG_LEDS_ENABLE)
/* MASTER / NODE_GUARDING / LSS / STORAGE / GTW / SRDO / TRACE / GFC 均不定义 = 关闭 */

#ifdef __cplusplus
extern "C" {
#endif

/* Stack configuration override default values. For more information see file CO_config.h. */

/* Basic definitions. If big endian, CO_SWAP_xx macros must swap bytes. */
#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) x
#define CO_SWAP_32(x) x
#define CO_SWAP_64(x) x
/* NULL is defined in stddef.h */
/* true and false are defined in stdbool.h */
/* int8_t to uint64_t are defined in stdint.h */
typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

/* ============================================================================
 *  接收帧消息对象：由 CO_CANinterrupt() 填充，传给各对象 CANrx_callback()
 * ========================================================================== */
typedef struct {
    uint32_t ident;   /**< 11-bit 标准 CAN 标识符（bit11 兼作 RTR 标志，与驱动层一致） */
    uint8_t  DLC;     /**< 数据长度代码（0..8） */
    uint8_t  data[8]; /**< 数据字节 */
} CO_CANrxMsg_t;

/* CANrx_callback() 从接收帧读取标识符 / DLC / 数据指针 */
#define CO_CANrxMsg_readIdent(msg) ((uint16_t)(((CO_CANrxMsg_t *)(msg))->ident))
#define CO_CANrxMsg_readDLC(msg)   ((uint8_t )(((CO_CANrxMsg_t *)(msg))->DLC))
#define CO_CANrxMsg_readData(msg)  ((const uint8_t *)(((CO_CANrxMsg_t *)(msg))->data))

/* Received frame object */
typedef struct {
    uint16_t ident;
    uint16_t mask;
    void* object;
    void (*CANrx_callback)(void* object, void* message);
} CO_CANrx_t;

/* Transmit frame object */
typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} CO_CANtx_t;

/* CAN module object */
typedef struct {
    void* CANptr;
    CO_CANrx_t* rxArray;
    uint16_t rxSize;
    CO_CANtx_t* txArray;
    uint16_t txSize;
    uint16_t CANerrorStatus;
    volatile bool_t CANnormal;
    volatile bool_t useCANrxFilters;
    volatile bool_t bufferInhibitFlag;
    volatile bool_t firstCANtxMessage;
    volatile uint16_t CANtxCount;
    uint32_t errOld;
} CO_CANmodule_t;

/* Data storage object for one entry */
typedef struct {
    void* addr;
    size_t len;
    uint8_t subIndexOD;
    uint8_t attr;
    /* Additional variables (target specific) */
    void* addrNV;
} CO_storage_entry_t;

/* (un)lock critical section in CO_CANsend() */
/* 裸机无 RTOS：临界区用全局关中断实现（临界段极短，不会造成 UART 接收溢出）。 */
#define CO_LOCK_CAN_SEND(CAN_MODULE)   __disable_irq()
#define CO_UNLOCK_CAN_SEND(CAN_MODULE) __enable_irq()

/* (un)lock critical section in CO_errorReport() or CO_errorReset() */
#define CO_LOCK_EMCY(CAN_MODULE)       __disable_irq()
#define CO_UNLOCK_EMCY(CAN_MODULE)     __enable_irq()

/* (un)lock critical section when accessing Object Dictionary */
#define CO_LOCK_OD(CAN_MODULE)         __disable_irq()
#define CO_UNLOCK_OD(CAN_MODULE)       __enable_irq()

/* Synchronization between CAN receive and data processing threads. */
/* 裸机下接收在 ISR 中完成预处理，主循环轮询处理；编译器屏障即可。 */
#define CO_MemoryBarrier()             __nop()
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)                                                                                             \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = (void*)1L;                                                                                             \
    }
#define CO_FLAG_CLEAR(rxNew)                                                                                           \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = NULL;                                                                                                  \
    }

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CO_DRIVER_TARGET_H */
