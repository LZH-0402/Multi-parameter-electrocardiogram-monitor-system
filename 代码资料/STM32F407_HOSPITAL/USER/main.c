#include "stm32f4xx.h"
#include "sys.h"                 /* uwTick / millis / delay_ms / SysTick_Init */
#include "debug.h"               /* DEBUG_UART_Init / dbg_puts / dbg_put_u32 */
#include "merge_cfg.h"

#include "usart.h"              /* USART2_Screen_Init / USART6_UNO_Init */
#include "dc_hmi.h"             /* HMI_Init / HMI_SetScreen / HMI_Poll */
#include "hmi_ids.h"            /* SCR_MAIN 等屏页 / 控件 ID */
#include "uno.h"                /* g_meas / UNO_WaveTake / g_uno_rx_bytes / g_uno_frames */
#include "app_bloodoxygen.h"    /* app_hmi_init / on_uno_frame / task_hmi_* */

#include "app_bp.h"            /* app_init / app_loop */

/* 血氧屏波特率：需与 SPO2_Hospital.tftprj 的 DeviceBaudRate(=7=115200) 一致 */
#define SCREEN_BAUD  115200u
/* UNO(AFE4490) 波特率：需与 UNO 上运行的 sketch Serial.begin() 一致 */
#define UNO_BAUD     57600u

/*
 * 双屏双传感器并行架构：
 *   USART1  调试口   115200          （debug.c）
 *   USART2  血氧屏   115200  dc_hmi   （P3 → SP3232 通道1 → COM_M）
 *   USART3  血压屏   9600    hmi_dacai（P5 → SP3232 通道2 → COM_G）
 *   UART5   血压传感器 115200         （PC12/PD2，TTL）
 *   USART6  血氧传感器 57600          （uno.c）
 * 主循环把两套子系统都跑起来：血氧屏任务 + 血压屏/传感器任务。
 */
/* 两位十进制补零（诊断打印用）。把 v(0~99) 写进 p，返回写后的新指针；v>=100 只取低两位。 */
static char *p2(char *p, uint8_t v)
{
    *p++ = (char)('0' + (v / 10) % 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

/* 打印 MCU 自己的软 RTC（= 血氧屏 RTC 的来源，固定基准后血压侧也完全相同），格式 YYYY-MM-DD HH:MM:SS。
 * 用途：把上位机这行时间和两块屏上的时间逐秒对照，就能定位剩余偏差在哪一侧，
 *       进而用 app_bloodoxygen.c 的 SPO2_RTC_OFFSET_SEC 一次性补掉。 */
static void dbg_rtc(void)
{
    uint16_t yy;
    uint8_t  mo, dd, hh, mi, ss;
    char     tb[24];
    char    *p = tb;

    spo2_rtc_get(&yy, &mo, &dd, &hh, &mi, &ss);
    p = p2(p, (uint8_t)((yy / 100u) % 100u));
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

/* 主入口。初始化调试口、血氧屏、血氧传感器、血压子系统后进入死循环。
 * 循环体内先跑血氧屏子系统，再跑血压子系统；所有节拍由 SysTick 的 uwTick 提供。 */
int main(void)
{
    /* startup 已调用 SystemInit() 把系统时钟配到 168MHz(HSE 8MHz->PLL)。
       SysTick 只初始化一次，1ms 节拍累加到 uwTick（SPO2 的 sys.c 定义）。
       BP 的 g_tick 已通过 app_bp.h 的 #define 别名到 uwTick，两套计时统一为同一个计数器。 */
    uint32_t t_hb = 0;

    SysTick_Init();             /* 1ms tick => uwTick */
    DEBUG_UART_Init();          /* USART1 @115200 调试口 */
    dbg_puts("\r\n==== STM32F407 HOSPITAL MONITOR BOOT OK (dual-screen) ====\r\n");

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
    app_init();
    dbg_puts("HMI: BP screen on USART3 @9600 (hmi_dacai); HKB-08B+ on UART5 @115200\r\n");

    dbg_puts("waiting for frames...\r\n");

    while (1)
    {
        /* 每秒一次心跳：确认程序在跑、时钟正确，并打印关键计数 */
        if (millis() - t_hb >= 1000)
        {
            t_hb = millis();
            dbg_puts("tick ");
            dbg_put_u32(t_hb / 1000);
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
            /* 调试（PR=0 排查）：对比 UNO 原算 HR 与 F407 峰检状态。
             *  若 uno_hr 稳定而 f407_ok=0 → 问题在 F407 峰检；
             *  若 uno_hr 也同时掉 0 → 问题在上游 UNO/信号源。 */
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
                /* 调试：信号质量来源排查。
                   sq  = 实际显示值 g_meas.signal_quality（受 UNO 中断占位写与 F407 双写竞争）
                   sqF = F407 端实测 SQI HMI_WaveGetSQI() */
                dbg_puts(" sq=");
                dbg_put_u32((uint32_t)g_meas.signal_quality);
                dbg_puts(" sqF=");
                dbg_put_u32((uint32_t)HMI_WaveGetSQI());
                /* 调试：峰检幅度/阈值。判读见 dc_hmi.c 的 g_ppg_dbg_* 说明：
                   thr≈ac → 正常；thr>>ac → 阈值被伪迹顶高、卡死；两者都极小 → 信号太弱。 */
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

            dbg_rtc();              /* MCU 软 RTC：用于对照两块屏的时间 */
            dbg_puts("\r\n");
        }

        /* ===== 血氧屏子系统 ===== */
        HMI_Poll();             /* 处理血氧屏上行按键通知 */
        /* PPG 波形：把中断里积压的采样全部取出推给屏。
         * 不再用 g_new_frame 单标志做门控 —— 主循环被血压屏 9600 阻塞发送占用时，
         * 单标志会把多帧合并成一个点，导致波形"一秒卡一次"。无新采样时此调用立即返回。 */
        on_uno_frame();
        task_hmi_value();       /* 数值/状态刷新：每 400ms（含报警判定 / 报警记录） */
        task_hmi_trend();       /* 趋势：每 2s 追加一点 */
        task_clock();           /* 软 RTC：每秒推进并同步到屏 */
        g_meas.monitor_sec = millis() / 1000;

        /* ===== 血压子系统（血压屏 + 血压传感器） ===== */
        app_loop();             /* 处理传感器环形缓冲、软 RTC、血压屏刷新 */
    }
}
