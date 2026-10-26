/*
 * CAN module object for STM32F407 (bxCAN, SPL) - CANopenNode 硬件驱动实现
 *
 * @file        CO_driver.c
 * @ingroup     CO_driver
 * @author      双屏监护仪 Node-A 移植（基于 CANopenNode 空白模板）
 *
 * 硬件：STM32F407ZGT6，CAN1 挂载 APB1（PCLK1 = 42MHz），引脚 PA11(CAN_RX)/PA12(CAN_TX)，
 *       复用功能 AF9。波特率固定 500kbps（与监护仪 CAN 协议 v0.1 一致）。
 *       裸机（无 RTOS），中断驱动接收/发送，主循环驱动协议栈。
 *
 * 本文件是 CANopenNode 与具体 MCU 之间的适配层，不属于 CANopenNode 原栈。
 */

#include "301/CO_driver.h"

#include "stm32f4xx.h"
#include "stm32f4xx_can.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "misc.h"

/* 本地错误计数，供 CO_CANmodule_process() 计算 CANerrorStatus */
static volatile uint16_t rxErrors = 0;
static volatile uint16_t txErrors = 0;
static volatile uint8_t  overflow = 0;

/* ============================================================================
 * 配置模式：请求 CAN 进入初始化模式并等待确认
 * 注意：CO_CANinit() 在 CO_CANmodule_init() 之前调用本函数，故必须在此先使能
 *       CAN1 时钟，否则等待 INAK 会死循环。
 * ========================================================================== */
void
CO_CANsetConfigurationMode(void* CANptr) {
    (void)CANptr;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    CAN1->MCR |= CAN_MCR_INRQ;   /* 请求初始化模式 */
    /* 等待 INAK 置位（带超时，避免极端情况下死锁） */
    for (volatile uint32_t t = 0; t < 0x10000UL; t++) {
        if ((CAN1->MSR & CAN_MSR_INAK) != 0) {
            break;
        }
    }
}

/* ============================================================================
 * 正常模式：置 CANnormal 标志（硬件已在 CO_CANmodule_init 中由 CAN_Init 进入正常模式）
 * ========================================================================== */
void
CO_CANsetNormalMode(CO_CANmodule_t* CANmodule) {
    if (CANmodule != NULL) {
        CANmodule->CANnormal = true;
    }
}

/* ============================================================================
 * 模块初始化
 * ========================================================================== */
CO_ReturnError_t
CO_CANmodule_init(CO_CANmodule_t* CANmodule, void* CANptr, CO_CANrx_t rxArray[], uint16_t rxSize,
                  CO_CANtx_t txArray[], uint16_t txSize, uint16_t CANbitRate) {
    uint16_t i;
    GPIO_InitTypeDef        gpio;
    CAN_InitTypeDef         can;
    CAN_FilterInitTypeDef   filter;
    NVIC_InitTypeDef        nvic;

    (void)CANptr;
    (void)CANbitRate;   /* 本项目固定 500kbps，见下方 BTR 配置 */

    /* 参数检查 */
    if (CANmodule == NULL || rxArray == NULL || txArray == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    /* 配置对象变量 */
    CANmodule->CANptr = CANptr;
    CANmodule->rxArray = rxArray;
    CANmodule->rxSize = rxSize;
    CANmodule->txArray = txArray;
    CANmodule->txSize = txSize;
    CANmodule->CANerrorStatus = 0;
    CANmodule->CANnormal = false;
    CANmodule->useCANrxFilters = false;   /* 软件过滤 + 硬件“全接收”过滤器 */
    CANmodule->bufferInhibitFlag = false;
    CANmodule->firstCANtxMessage = true;
    CANmodule->CANtxCount = 0U;
    CANmodule->errOld = 0U;

    for (i = 0U; i < rxSize; i++) {
        rxArray[i].ident = 0U;
        rxArray[i].mask = 0xFFFFU;
        rxArray[i].object = NULL;
        rxArray[i].CANrx_callback = NULL;
    }
    for (i = 0U; i < txSize; i++) {
        txArray[i].bufferFull = false;
    }

    /* ---- 时钟与 GPIO（PA11=CAN_RX, PA12=CAN_TX, AF9）---- */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource11, GPIO_AF_CAN1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource12, GPIO_AF_CAN1);
    gpio.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &gpio);

    /* ---- bxCAN 配置：500kbps @ PCLK1=42MHz ----
     * 位时间 = (1 + BS1 + BS2) TQ；CAN 时钟 = 42MHz / Prescaler(6) = 7MHz；
     * 每帧 14 TQ → 500kbps；采样点 = (1+11)/14 ≈ 85.7%。
     * 注意：CAN_Init() 内部会自行请求并等待初始化模式（CO_CANsetConfigurationMode
     * 已置 INRQ），无需在此调用 CAN_DeInit 复位外设。 */
    can.CAN_TTCM = DISABLE;           /* 关闭时间触发通信 */
    can.CAN_ABOM = ENABLE;            /* 自动离线恢复（Bus-Off 后自动恢复） */
    can.CAN_AWUM = ENABLE;            /* 自动唤醒 */
    can.CAN_NART = DISABLE;           /* 自动重传（CAN 要求可靠传输） */
    can.CAN_RFLM = DISABLE;           /* FIFO 满时覆盖（并记录溢出） */
    can.CAN_TXFP = DISABLE;           /* 发送顺序按报文 ID 优先级 */
    can.CAN_Mode = CAN_Mode_Normal;
    can.CAN_SJW  = CAN_SJW_1tq;
    can.CAN_BS1  = CAN_BS1_11tq;
    can.CAN_BS2  = CAN_BS2_2tq;
    can.CAN_Prescaler = 6;
    CAN_Init(CAN1, &can);             /* 内部：进入初始化模式→写寄存器→回到正常模式 */

    /* ---- 接收过滤器：bank0，屏蔽模式，掩码=0 → 接收所有标准帧进 FIFO0 ----
     * 关键：bxCAN 若没有任何激活的过滤器，所有接收帧都会被丢弃。
     * 这里用一个“全接收”过滤器，具体帧的过滤交由驱动软件层（useCANrxFilters=false）。 */
    filter.CAN_FilterNumber = 0;
    filter.CAN_FilterMode = CAN_FilterMode_IdMask;
    filter.CAN_FilterScale = CAN_FilterScale_32bit;
    filter.CAN_FilterIdHigh = 0x0000;
    filter.CAN_FilterIdLow = 0x0000;
    filter.CAN_FilterMaskIdHigh = 0x0000;   /* 掩码全 0 = 不关心任何位 = 全接收 */
    filter.CAN_FilterMaskIdLow = 0x0000;
    filter.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    filter.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&filter);

    /* ---- 中断使能：FIFO0 接收 / 发送完成 / 错误(SCE) / FIFO0 溢出 ---- */
    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME | CAN_IT_ERR | CAN_IT_FOV0, ENABLE);

    /* NVIC：CAN 中断优先级设为 3（最低），确保不抢占 UART 接收中断
     *（UART 用于血氧/血压传感器高速流，时间关键）。 */
    nvic.NVIC_IRQChannel = CAN1_RX0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    nvic.NVIC_IRQChannel = CAN1_TX_IRQn;
    NVIC_Init(&nvic);

    nvic.NVIC_IRQChannel = CAN1_SCE_IRQn;
    NVIC_Init(&nvic);

    return CO_ERROR_NO;
}

/* ============================================================================
 * 关闭 CAN 模块
 * ========================================================================== */
void
CO_CANmodule_disable(CO_CANmodule_t* CANmodule) {
    if (CANmodule != NULL) {
        CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME | CAN_IT_ERR | CAN_IT_FOV0, DISABLE);
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, DISABLE);  /* F4 SPL 无 CAN_Cmd：改用关闭 CAN1 时钟禁用外设 */
        CANmodule->CANnormal = false;
    }
}

/* ============================================================================
 * 配置接收缓冲区（软件过滤模式：仅记录 ident/mask/回调，不改硬件过滤器）
 * ========================================================================== */
CO_ReturnError_t
CO_CANrxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, uint16_t mask, bool_t rtr, void* object,
                   void (*CANrx_callback)(void* object, void* message)) {
    CO_ReturnError_t ret = CO_ERROR_NO;

    if ((CANmodule != NULL) && (object != NULL) && (CANrx_callback != NULL) && (index < CANmodule->rxSize)) {
        CO_CANrx_t* buffer = &CANmodule->rxArray[index];

        buffer->object = object;
        buffer->CANrx_callback = CANrx_callback;

        /* ident 与 mask 按 CAN 模块位对齐：标准 11 位 + RTR(bit11) */
        buffer->ident = ident & 0x07FFU;
        if (rtr) {
            buffer->ident |= 0x0800U;
        }
        buffer->mask = (mask & 0x07FFU) | 0x0800U;
    } else {
        ret = CO_ERROR_ILLEGAL_ARGUMENT;
    }

    return ret;
}

/* ============================================================================
 * 配置发送缓冲区
 * ========================================================================== */
CO_CANtx_t*
CO_CANtxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, bool_t rtr, uint8_t noOfBytes,
                   bool_t syncFlag) {
    CO_CANtx_t* buffer = NULL;

    if ((CANmodule != NULL) && (index < CANmodule->txSize)) {
        buffer = &CANmodule->txArray[index];

        /* ident 位对齐：bit0..10=标准 ID，bit11..14=DLC，bit15=RTR */
        buffer->ident = ((uint32_t)ident & 0x07FFU)
                        | ((uint32_t)(((uint32_t)noOfBytes & 0xFU) << 11U)
                           | ((uint32_t)(rtr ? 0x8000U : 0U)));
        buffer->DLC = noOfBytes;     /* 显式记录 DLC，供 CO_CANsend 使用 */
        buffer->bufferFull = false;
        buffer->syncFlag = syncFlag;
    }

    return buffer;
}

/* ============================================================================
 * 发送 CAN 帧
 * ========================================================================== */
CO_ReturnError_t
CO_CANsend(CO_CANmodule_t* CANmodule, CO_CANtx_t* buffer) {
    CO_ReturnError_t err = CO_ERROR_NO;

    /* 溢出检测 */
    if (buffer->bufferFull) {
        if (!CANmodule->firstCANtxMessage) {
            CANmodule->CANerrorStatus |= CO_CAN_ERRTX_OVERFLOW;
        }
        err = CO_ERROR_TX_OVERFLOW;
    }

    CO_LOCK_CAN_SEND(CANmodule);
    /* 若没有排队帧且存在空闲发送邮箱 → 直接发送 */
    if ((CANmodule->CANtxCount == 0U)
        && ((CAN1->TSR & (CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2)) != 0U)) {
        CanTxMsg t;
        t.StdId = buffer->ident & 0x07FFU;
        t.ExtId = 0U;
        t.IDE = CAN_Id_Standard;
        t.RTR = ((buffer->ident & 0x8000U) != 0U) ? CAN_RTR_Remote : CAN_RTR_Data;
        t.DLC = (buffer->DLC > 8U) ? 8U : buffer->DLC;
        for (uint8_t i = 0U; i < t.DLC; i++) {
            t.Data[i] = buffer->data[i];
        }
        {
            uint8_t mb = CAN_Transmit(CAN1, &t);
            if (mb == CAN_TxStatus_NoMailBox) {
                /* 极端情况：没有空闲邮箱，重新排队 */
                buffer->bufferFull = true;
                CANmodule->CANtxCount++;
            } else {
                CANmodule->bufferInhibitFlag = buffer->syncFlag;
            }
        }
    } else {
        /* 否则排队，由发送完成中断发送 */
        if (!buffer->bufferFull) {
            buffer->bufferFull = true;
            CANmodule->CANtxCount++;
        }
    }
    CO_UNLOCK_CAN_SEND(CANmodule);

    return err;
}

/* ============================================================================
 * 清除所有同步 TPDO（裸机不使用 SYNC 窗口，保留实现以备将来）
 * ========================================================================== */
void
CO_CANclearPendingSyncPDOs(CO_CANmodule_t* CANmodule) {
    uint32_t tpdoDeleted = 0U;

    CO_LOCK_CAN_SEND(CANmodule);
    if (CANmodule->bufferInhibitFlag) {
        CANmodule->bufferInhibitFlag = false;
        tpdoDeleted = 1U;
    }
    if (CANmodule->CANtxCount != 0U) {
        uint16_t i;
        CO_CANtx_t* buffer = &CANmodule->txArray[0];
        for (i = CANmodule->txSize; i > 0U; i--) {
            if (buffer->bufferFull) {
                if (buffer->syncFlag) {
                    buffer->bufferFull = false;
                    CANmodule->CANtxCount--;
                    tpdoDeleted = 2U;
                }
            }
            buffer++;
        }
    }
    CO_UNLOCK_CAN_SEND(CANmodule);

    if (tpdoDeleted != 0U) {
        CANmodule->CANerrorStatus |= CO_CAN_ERRTX_PDO_LATE;
    }
}

/* ============================================================================
 * 计算 CAN 错误状态（周期性调用）
 * ========================================================================== */
void
CO_CANmodule_process(CO_CANmodule_t* CANmodule) {
    uint32_t err;

    err = ((uint32_t)txErrors << 16) | ((uint32_t)rxErrors << 8) | overflow;

    if (CANmodule->errOld != err) {
        uint16_t status = CANmodule->CANerrorStatus;

        CANmodule->errOld = err;

        if (txErrors >= 256U) {
            status |= CO_CAN_ERRTX_BUS_OFF;      /* Bus-Off */
        } else {
            status &= 0xFFFFU
                      ^ (CO_CAN_ERRTX_BUS_OFF | CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE | CO_CAN_ERRTX_WARNING
                         | CO_CAN_ERRTX_PASSIVE);

            if (rxErrors >= 128) {
                status |= CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE;
            } else if (rxErrors >= 96) {
                status |= CO_CAN_ERRRX_WARNING;
            }

            if (txErrors >= 128) {
                status |= CO_CAN_ERRTX_WARNING | CO_CAN_ERRTX_PASSIVE;
            } else if (txErrors >= 96) {
                status |= CO_CAN_ERRTX_WARNING;
            }

            if ((status & CO_CAN_ERRTX_PASSIVE) == 0) {
                status &= 0xFFFFU ^ CO_CAN_ERRTX_OVERFLOW;
            }
        }

        if (overflow != 0) {
            status |= CO_CAN_ERRRX_OVERFLOW;
        }

        CANmodule->CANerrorStatus = status;
    }
}

/* ============================================================================
 * CAN 中断服务程序入口（由 stm32f4xx_it.c 的 CAN1_RX0/TX/SCE 向量统一调用）
 * ----------------------------------------------------------------------------
 *  bxCAN 有三个独立向量：RX0、TX、SCE(状态改变/错误)。
 *  本函数统一检查各标志位，可安全地从三个向量中的任意一个调用。
 * ========================================================================== */
void
CO_CANinterrupt(CO_CANmodule_t* CANmodule) {
    uint8_t i;
    uint16_t index;
    CO_CANrx_t* buffer;
    bool_t msgMatched;
    CanRxMsg rx;
    CO_CANrxMsg_t rcvMsg;

    /* ---------- 接收 FIFO0 ---------- */
    if ((CAN1->RF0R & CAN_RF0R_FMP0) != 0U) {
        /* 捕获 FIFO 溢出（在 CAN_Receive 释放邮箱前读取，否则会被自动清除） */
        if ((CAN1->RF0R & CAN_RF0R_FOVR0) != 0U) {
            overflow = 1U;
            CAN1->RF0R |= CAN_RF0R_FOVR0;   /* 写 1 清除溢出标志 */
        }

        CAN_Receive(CAN1, CAN_FIFO0, &rx);
        rcvMsg.ident = rx.StdId;
        rcvMsg.DLC = rx.DLC;
        for (i = 0U; i < rx.DLC && i < 8U; i++) {
            rcvMsg.data[i] = rx.Data[i];
        }

        /* 软件过滤：在 rxArray 中查找匹配的 ident/mask */
        buffer = &CANmodule->rxArray[0];
        msgMatched = false;
        for (index = CANmodule->rxSize; index > 0U; index--) {
            if (((rcvMsg.ident ^ buffer->ident) & buffer->mask) == 0U) {
                msgMatched = true;
                break;
            }
            buffer++;
        }

        if (msgMatched && (buffer->CANrx_callback != NULL)) {
            buffer->CANrx_callback(buffer->object, (void*)&rcvMsg);
        }
    }

    /* ---------- 发送完成 ---------- */
    if ((CAN1->TSR & (CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2)) != 0U) {
        CANmodule->firstCANtxMessage = false;
        CANmodule->bufferInhibitFlag = false;
        CAN1->TSR |= (CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2);   /* 写 1 清请求完成标志 */

        /* 发送下一个排队的帧 */
        if (CANmodule->CANtxCount > 0U) {
            CO_CANtx_t* txb = &CANmodule->txArray[0];
            for (i = CANmodule->txSize; i > 0U; i--) {
                if (txb->bufferFull) {
                    txb->bufferFull = false;
                    CANmodule->CANtxCount--;

                    {
                        CanTxMsg t;
                        t.StdId = txb->ident & 0x07FFU;
                        t.ExtId = 0U;
                        t.IDE = CAN_Id_Standard;
                        t.RTR = ((txb->ident & 0x8000U) != 0U) ? CAN_RTR_Remote : CAN_RTR_Data;
                        t.DLC = (txb->DLC > 8U) ? 8U : txb->DLC;
                        for (index = 0U; index < t.DLC; index++) {
                            t.Data[index] = txb->data[index];
                        }
                        {
                            uint8_t mb = CAN_Transmit(CAN1, &t);
                            if (mb == CAN_TxStatus_NoMailBox) {
                                /* 极端情况下没有空闲邮箱，重新排队 */
                                txb->bufferFull = true;
                                CANmodule->CANtxCount++;
                            }
                        }
                    }
                    break;   /* 退出循环（每轮中断最多发一帧） */
                }
                txb++;
            }
            if (i == 0U) {
                CANmodule->CANtxCount = 0U;
            }
        }
    }

    /* ---------- 错误 / 状态改变（SCE）---------- */
    if ((CAN1->MSR & CAN_MSR_ERRI) != 0U) {
        uint32_t esr = CAN1->ESR;
        rxErrors = (uint16_t)((esr >> 16) & 0xFFU);   /* REC：接收错误计数 */
        txErrors = (uint16_t)((esr >> 24) & 0xFFU);   /* TEC：发送错误计数 */
        if ((CAN1->RF0R & CAN_RF0R_FOVR0) != 0U) {
            overflow = 1U;
            CAN1->RF0R |= CAN_RF0R_FOVR0;
        }
        CAN1->MSR |= CAN_MSR_ERRI;   /* 写 1 清除 ERRI */
    }
}
