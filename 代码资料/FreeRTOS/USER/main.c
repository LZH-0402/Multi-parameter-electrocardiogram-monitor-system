/**
  ******************************************************************************
  * @file    FreeRTOS / USER / main.c
  * @brief   STM32F407 多参数心电监护仪 —— FreeRTOS 移植版
  *
  * 移植说明：
  *   原裸机工程 STM32F407_HOSPITAL 是单线程"超级循环"，现按 F:\FreeRTOS 示例
  *   （xTaskCreate + start_task + vTaskStartScheduler）的方式，把主循环拆成
  *   若干个 FreeRTOS 任务：
  *     - task_spo2     ：血氧屏子系统（HMI_Poll / 波形 / 数值 / 趋势 / 软RTC）
  *     - task_bp       ：血压子系统（血压屏 + 血压传感器，app_loop 自管理）
  *     - task_heartbeat：调试口每秒打印计数，兼做 LED 呼吸指示
  *
  * 时钟：SysTick 由 FreeRTOS 内核接管（configTICK_RATE_HZ=1000 => 1ms 节拍）。
  *       原裸机的 uwTick / millis() 仍被应用层大量使用，因此通过
  *       vApplicationTickHook()（configUSE_TICK_HOOK=1）在每个 tick 里递增 uwTick，
  *       保持软 RTC 与所有基于 millis() 的节拍不变。
  *       —— 故本文件**不再调用** SysTick_Init()，避免与 FreeRTOS 抢占 SysTick。
  ******************************************************************************
  */

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"

#include "sys.h"                 /* uwTick / millis —— 由 vApplicationTickHook 递增 */
#include "led.h"                /* LED_Init（FreeRTOS 心跳指示，可选） */
#include "debug.h"              /* DEBUG_UART_Init / dbg_puts / dbg_put_u32 */
#include "merge_cfg.h"

#include "usart.h"              /* USART2_Screen_Init / USART6_UNO_Init */
#include "dc_hmi.h"             /* HMI_Init / HMI_SetScreen / HMI_Poll */
#include "hmi_ids.h"            /* SCR_MAIN 等屏页 / 控件 ID */
#include "uno.h"                /* g_meas / UNO_WaveTake / g_uno_rx_bytes / g_uno_frames */
#include "app_bloodoxygen.h"    /* app_hmi_init / on_uno_frame / task_hmi_* */
#include "app_bp.h"             /* app_init / app_loop */

/* 血氧屏波特率：需与 SPO2_Hospital.tftprj 的 DeviceBaudRate(=7=115200) 一致 */
#define SCREEN_BAUD  115200u
/* UNO(AFE4490) 波特率：需与 UNO 上运行的 sketch Serial.begin() 一致 */
#define UNO_BAUD     57600u

/* ---------------- 任务参数 ---------------- */
#define START_TASK_PRIO     4
#define START_STK_SIZE      256
TaskHandle_t StartTask_Handler;
void start_task(void *pvParameters);

#define SPO2_TASK_PRIO      3
#define SPO2_STK_SIZE       1024
TaskHandle_t Spo2Task_Handler;
void task_spo2(void *pvParameters);

#define BP_TASK_PRIO        2
#define BP_STK_SIZE         1024
TaskHandle_t BpTask_Handler;
void task_bp(void *pvParameters);

#define HB_TASK_PRIO        1
#define HB_STK_SIZE         256
TaskHandle_t HbTask_Handler;
void task_heartbeat(void *pvParameters);

/* 两位十进制补零（诊断打印用）。把 v(0~99) 写进 p，返回写后的新指针；v>=100 只取低两位。 */
static char *p2(char *p, uint8_t v)
{
    *p++ = (char)('0' + (v / 10) % 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

/* 打印 MCU 自己的软 RTC（= 血氧屏 RTC 的来源），格式 YYYY-MM-DD HH:MM:SS。 */
static void dbg_rtc(void)
{
    uint16_t yy;
    uint8_t  mo, dd, hh, mi, ss;
    char     tb[24];
    char    *p = tb;

    spo2_rtc_get(&yy, &mo, &dd, &hh, &mi, &ss);
    p = p2(p, (uint8_t)((yy / 100) % 100u));
    p = p2(p, (uint8_t)(yy % 100u));
    *p++ = '-'; p = p2(p, mo);
    *p++ = '-'; p = p2(p, dd);
    *p++ = ' '; p = p2(p, hh);
    *p++ = ':'; p = p2(p, mi);
    *p++ = ':'; p = p2(p, ss);
    *p = '\0';
    dbg_puts(" rtc=");
    dbg_puts(tb);
}

/* ============================================================================
 * FreeRTOS 节拍钩子：每个 SysTick（1ms）调用一次，递增裸机 uwTick，
 * 使 millis() / 软 RTC 计时在 RTOS 下继续生效。
 * 注意：本函数在中断上下文执行，必须极短；仅做一个 volatile 自增。
 * ==========================================================================*/
void vApplicationTickHook(void)
{
    uwTick++;
}

int main(void)
{
    /* startup 已调用 SystemInit() 把系统时钟配到 168MHz(HSE 8MHz->PLL)。
       FreeRTOS 内核会在 vTaskStartScheduler() 内接管 SysTick（1ms 节拍）。
       NVIC 优先级分组必须设为 4（全部为抢占优先级），这是 FreeRTOS Cortex-M
       移植的硬性要求，否则内核 PendSV/SysTick 的最低优先级无法保证。 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    LED_Init();                 /* PF9/PF10 心跳指示（无 LED 也不影响功能） */

    DEBUG_UART_Init();          /* USART1 @115200 调试口 */
    dbg_puts("\r\n==== STM32F407 HOSPITAL MONITOR BOOT OK (FreeRTOS dual-screen) ====\r\n");

    /* ---- 血氧屏：USART2 @115200, RXNE 中断, dc_hmi 驱动 ---- */
    USART2_Screen_Init(SCREEN_BAUD);
    HMI_Init();
    HMI_SetScreen(SCR_MAIN);
    app_hmi_init();             /* 屏 RTC / 趋势时段 / 初始背光 / 报警记录清空 */
    dbg_puts("HMI: SPO2 screen on USART2 @115200 (dc_hmi)\r\n");

    /* ---- 血氧传感器 UNO(AFE4490)：USART6 @57600 ---- */
    USART6_UNO_Init(UNO_BAUD);
    dbg_puts("UNO(SpO2 sensor) on USART6 @57600 ready\r\n");

    /* ---- 血压子系统：血压屏 USART3 @9600 (hmi_dacai) + 血压传感器 UART5 @115200 ---- */
    app_init();                 /* 内部自行初始化 USART3/UART5/DMA/NVIC */
    dbg_puts("HMI: BP screen on USART3 @9600 (hmi_dacai); HKB-08B+ on UART5 @115200\r\n");

    dbg_puts("waiting for frames...\r\n");

    /* 创建启动任务，由它再创建各业务任务后自删除（与 FreeRTOS 示例一致）。 */
    xTaskCreate((TaskFunction_t )start_task,
                (const char*    )"start_task",
                (uint16_t       )START_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )START_TASK_PRIO,
                (TaskHandle_t*  )&StartTask_Handler);

    vTaskStartScheduler();      /* 启动调度器：此后 SysTick 由 FreeRTOS 掌管 */

    /* 若调度器启动失败（内存不足等）才会走到这里 */
    while (1)
    {
    }
}

/**
  * @brief  启动任务：创建业务任务后自删除（避免长期占用内存）。
  */
void start_task(void *pvParameters)
{
    (void)pvParameters;

    taskENTER_CRITICAL();

    xTaskCreate((TaskFunction_t )task_spo2,
                (const char*    )"task_spo2",
                (uint16_t       )SPO2_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )SPO2_TASK_PRIO,
                (TaskHandle_t*  )&Spo2Task_Handler);

    xTaskCreate((TaskFunction_t )task_bp,
                (const char*    )"task_bp",
                (uint16_t       )BP_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )BP_TASK_PRIO,
                (TaskHandle_t*  )&BpTask_Handler);

    xTaskCreate((TaskFunction_t )task_heartbeat,
                (const char*    )"task_heartbeat",
                (uint16_t       )HB_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )HB_TASK_PRIO,
                (TaskHandle_t*  )&HbTask_Handler);

    vTaskDelete(StartTask_Handler);

    taskEXIT_CRITICAL();
}

/**
  * @brief  血氧屏子系统任务。
  *         原裸机主循环里"每秒心跳"之外的全部血氧相关调用都搬到这里，
  *         内部各函数（task_hmi_value/task_hmi_trend/task_clock）仍按自身
  *         millis() 节拍决定是否真正动作，因此只需每 ~2ms 轮询一次即可。
  */
void task_spo2(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        HMI_Poll();             /* 处理血氧屏上行按键通知 */
        /* PPG 波形：把中断里积压的采样全部取出推给屏（暂停时丢弃积压）。 */
        on_uno_frame();
        task_hmi_value();        /* 数值/状态刷新：每 400ms（含报警判定 / 报警记录） */
        task_hmi_trend();        /* 趋势：每 2s 追加一点 */
        task_clock();            /* 软 RTC：每秒推进并同步到屏 */

        g_meas.monitor_sec = millis() / 1000;

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/**
  * @brief  血压子系统任务。
  *         app_loop() 内部自行管理传感器环形缓冲、软 RTC、血压屏刷新与节拍，
  *         这里只需周期性调用并让出 CPU。
  */
void task_bp(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        app_loop();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/**
  * @brief  心跳 / 调试打印任务：每秒在 USART1 上输出关键计数与软 RTC，
  *         并翻转 LED（PF9）指示 RTOS 正常运行。
  */
void task_heartbeat(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        GPIO_ToggleBits(GPIOF, GPIO_Pin_9);

        dbg_puts("tick ");
        dbg_put_u32(millis() / 1000);
        dbg_puts(" | hmi_rx=");
        dbg_put_u32(g_hmi_rx_bytes);
        dbg_puts(" | uno_rx=");
        dbg_put_u32(g_uno_rx_bytes);
        dbg_puts(" frames=");
        dbg_put_u32(g_uno_frames);
        dbg_puts(" | spo2=");
        dbg_put_u32((uint32_t)g_meas.spo2);
        dbg_puts(" pr=");
        dbg_put_u32((uint32_t)g_meas.pulse_rate);

#if DEBUG_PPG
        {
            uint8_t  d_ok = 0, d_prom = 0;
            int16_t  d_est = 0;
            HMI_WaveGetPRDetail(&d_ok, &d_est, &d_prom);
            dbg_puts(" | uno_hr=");
            dbg_put_u32((uint32_t)g_uno_hr);
            dbg_puts(" f407_ok=");
            dbg_put_u32((uint32_t)d_ok);
            dbg_puts(" f407_est=");
            dbg_put_u32((uint32_t)d_est);
            dbg_puts(" prom=");
            dbg_put_u32((uint32_t)d_prom);
            dbg_puts(" rst=");
            dbg_put_u32(g_ppg_rst_cnt);
            dbg_puts(" n=");
            dbg_put_u32((uint32_t)g_uno_last_n);
            dbg_puts(" ls=");
            dbg_put_u32((uint32_t)g_pr_loss_streak);
            dbg_puts(" wv=");
            dbg_put_u32((uint32_t)g_last_pr_valid);
            dbg_puts(" wok=");
            dbg_put_u32((uint32_t)g_last_pr_ok);
            dbg_puts(" west=");
            dbg_put_u32((uint32_t)g_last_pr_est);
            dbg_puts(" wpr=");
            dbg_put_u32((uint32_t)g_last_pr_write);
            dbg_puts(" sq=");
            dbg_put_u32((uint32_t)g_meas.signal_quality);
            dbg_puts(" sqF=");
            dbg_put_u32((uint32_t)HMI_WaveGetSQI());
            dbg_puts(" amp=");
            dbg_put_u32((uint32_t)g_ppg_dbg_amp);
            dbg_puts(" thr=");
            dbg_put_u32((uint32_t)g_ppg_dbg_thr);
            dbg_puts(" ac=");
            dbg_put_u32((uint32_t)g_ppg_dbg_ac);
            dbg_puts(" ab=");
            dbg_put_u32((uint32_t)g_ppg_dbg_above);
            dbg_puts(" pkc=");
            dbg_put_u32(g_ppg_dbg_pkcnt);
        }
#endif /* DEBUG_PPG */

        dbg_rtc();
        dbg_puts("\r\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
