#include "stm32f4xx.h"
#include "sys.h"
#include "debug.h"

/* ===== 分步验证开关 =====
   DEMO_STEP 决定本工程“跑到哪一步”，方便你逐段验证：
     1 = 仅基础：SysTick + USART1 心跳（确认时钟 / 串口正常）
     2 = 增加 USART2 屏驱动，向屏写死 SpO2/PR（确认屏链路）
     3 = 增加 USART6 收 UNO 帧并解析，解析值经 USART1 打印（确认 UNO 链路与解析）
     4 = 全功能：UNO -> STM32 -> 屏（真实数据上屏；默认）
   改这一个宏后重新编译即可切换阶段。 */
#define DEMO_STEP  4

/* 屏口(USART2)波特率，必须与屏工程 SPO2_Hospital.tftprj 的 DeviceBaudRate 一致。
 * 本工程屏：DeviceBaudRate="7" -> 115200。
 * 【排障提示】血压工程同一块屏用的是 9600（DeviceBaudRate="3"）且实测能跑；
 *   若 115200 下屏无反应，先把这里改成 9600 重新编译再试，即可排除波特率因素。 */
#define SCREEN_BAUD  115200

/* UNO（USART6，PC6/PC7）波特率。
 * ★必须与 UNO 上跑的 sketch 一致：
 *   AFE4490_library 的 Example-1（唯一发 17 字节二进制帧的那个）用的是
 *   Serial.begin(57600)  -> 57600。
 *   （Example-2/Example-3 分别是 115200/57600 但只打 ASCII 文本，不能用。）
 * 下面 main() 里还会做一次自动探测，所以这里填错也不会致命。 */
#define UNO_BAUD     57600

/* 排障开关：1 = 每 1s 把 USART6 收到的最近 16 字节以 hex 打到 USART1（抓包定位用）；
 *           0 = 关闭，USART1 只留一行 tick 心跳（演示/交付用，日志干净）。
 * 链路已实测跑通，故默认 0；以后换 UNO 板/改 sketch 时改回 1 即可。 */
#define UNO_RAW_DUMP   0

#if DEMO_STEP >= 2
#include "dc_hmi.h"
#include "hmi_ids.h"
#include "usart.h"
#endif
#if DEMO_STEP >= 3
#include "uno.h"
#endif
#if DEMO_STEP >= 4
#include "app_bloodoxygen.h"
#endif

#if DEMO_STEP >= 2
/* 当前实际使用的屏波特率（由自动探测得出） */
static uint32_t g_screen_baud = SCREEN_BAUD;

/* 屏波特率自动探测：依次用候选波特率发“查询画面”EE B1 01，
 * 哪个波特率收到屏的应答，就用哪个，并打印到调试口。
 *
 * 本工程这块屏实测是 115200（血压工程那块同型号屏才是 9600），
 * 探测只为兜底：万一换了屏/改了 DeviceBaudRate，不必手改代码。
 * 真正的坑不在波特率，而在“屏不认批量指令(B1 12 + B3)”，见 dc_hmi/app 层注释。 */
static uint32_t screen_autobaud(void)
{
    static const uint32_t cand[2] = { 115200u, 9600u };
    uint32_t i;
    for (i = 0; i < 2u; i++)
    {
        uint32_t before, t0;
        USART2_Screen_Init(cand[i]);
        delay_ms(20);
        before = g_hmi_rx_bytes;
        HMI_GetScreen();                       /* EE B1 01 */
        t0 = millis();
        while ((millis() - t0) < 250u) { }     /* 等屏应答（IRQ 里累计字节数） */
        if (g_hmi_rx_bytes >= before + 8u)     /* 收到一整帧应答 */
        {
            dbg_puts("screen auto-baud: ANSWER @");
            dbg_put_u32(cand[i]);·
            dbg_puts(" bps\r\n");
            return cand[i];
        }
        dbg_puts("screen auto-baud: no reply @");
        dbg_put_u32(cand[i]);
        dbg_puts(" bps\r\n");
    }
    dbg_puts("screen auto-baud: no baud worked, keep ");
    dbg_put_u32(SCREEN_BAUD);
    dbg_puts("\r\n");
    return SCREEN_BAUD;
}
#endif

#if DEMO_STEP >= 3
/* UNO 波特率自动探测：依次用候选波特率听 0.8s，
 * 哪个波特率解析出的完整帧最多就用哪个。
 * 因为帧头 5 字节 + 帧尾 2 字节校验足够强，波特率不对时几乎不可能误判成有效帧。 */
static uint32_t uno_autobaud(void)
{
    static const uint32_t cand[3] = { 57600u, 115200u, 9600u };
    uint32_t best = UNO_BAUD, best_n = 0;
    uint32_t i;

    for (i = 0; i < 3u; i++)
    {
        uint32_t t0, n0, b0;

        UNO_Reset();                       /* 清状态机与计数 */
        USART6_UNO_Init(cand[i]);
        t0 = millis();
        while ((millis() - t0) < 800u) { } /* 听 0.8s */
        n0 = g_uno_frames;
        b0 = g_uno_rx_bytes;

        dbg_puts("UNO auto-baud @");
        dbg_put_u32(cand[i]);
        dbg_puts("  rx_bytes=");
        dbg_put_u32(b0);
        dbg_puts("  frames=");
        dbg_put_u32(n0);
        dbg_puts("\r\n");

        if (n0 > best_n) { best_n = n0; best = cand[i]; }

        /* 已能稳定解析出帧的候选就是它：乱码几乎不可能凑出"5 字节头 + 00 0B 帧尾"，
           所以可立即锁定，省掉后面候选的等待（开机更快）。 */
        if (n0 >= 3u) break;
    }

    UNO_Reset();
    USART6_UNO_Init(best);
    return best;
}

/* 当前实际使用的 UNO 波特率（由自动探测得出） */
static uint32_t g_uno_baud = UNO_BAUD;
#endif

int main(void)
{
    /* startup 已调用 SystemInit() 把系统时钟配到 168MHz（HSE 8MHz -> PLL）。
       此处只需启动 SysTick 与各外设。 */
    uint32_t t_hb   = 0;
#if DEMO_STEP == 2
    uint32_t t_demo = 0;   /* 仅阶段2（写死值上屏测试）用到 */
    uint32_t s2_n   = 0;   /* 阶段2 发送计数（回显到调试口，便于定位） */
    uint32_t t_query = 0;  /* 阶段2 查询屏周期 */
    uint32_t last_rx = 0;  /* 上次查询时累计到的屏字节数 */
#endif
#if DEMO_STEP >= 3 && UNO_RAW_DUMP
    uint32_t last_uno_rx = 0;  /* 上次回显时的 UNO 字节数（变化才抓包） */
#endif

    SysTick_Init();
    DEBUG_UART_Init();
    dbg_puts("\r\n==== SPO2 BOOT OK ====\r\n");

#if DEMO_STEP >= 2
    USART2_Screen_Init(SCREEN_BAUD);
    HMI_Init();
    g_screen_baud = screen_autobaud();   /* 自动探测屏实际波特率（115200 / 9600） */
    HMI_SetScreen(SCR_MAIN);
    dbg_puts("screen USART2 @"); dbg_put_u32(g_screen_baud); dbg_puts(" bps\r\n");
#endif

#if DEMO_STEP >= 4
    app_hmi_init();   /* 应用层初始化：屏 RTC / 趋势时段 / 初始背光 / 报警记录清空 */
#endif

#if DEMO_STEP >= 3
    USART6_UNO_Init(UNO_BAUD);
    g_uno_baud = uno_autobaud();          /* 自动探测 UNO 实际波特率 */
    dbg_puts("UNO link USART6 @"); dbg_put_u32(g_uno_baud); dbg_puts(" bps ready\r\n");
#endif

    dbg_puts("waiting for frames...\r\n");

    while (1)
    {
        /* 每秒一次心跳：确认程序在跑、SysTick 与时钟正确（任何阶段都能看） */
        if (millis() - t_hb >= 1000)
        {
            t_hb = millis();
            dbg_puts("tick ");
            dbg_put_u32(t_hb / 1000);
#if DEMO_STEP >= 2
            /* ★ 诊断用：屏 -> MCU 的上行字节总数。
                 按键时这个数会跳增 14（按钮通知帧 EE B1 11 ...）
                 切页时跳增 9（画面切换通知 EE B1 01 <screen>）。
                 如果一直不动，说明屏根本没上报 -> 先查 DeviceControlNotify /
                 该按钮的"事件通知"是否被关成"否"。 */
            dbg_puts(" | hmi_rx="); dbg_put_u32(g_hmi_rx_bytes);
#endif
#if DEMO_STEP >= 3
            dbg_puts(" | uno_rx="); dbg_put_u32(g_uno_rx_bytes);
            dbg_puts(" frames=");   dbg_put_u32(g_uno_frames);
            dbg_puts(" | spo2="); dbg_put_u32((uint32_t)g_meas.spo2);
            dbg_puts(" pr=");     dbg_put_u32((uint32_t)g_meas.pulse_rate);
            dbg_puts(" ir=");     dbg_put_u32((uint32_t)g_meas.ir_raw);
#endif
#if DEMO_STEP >= 4
            dbg_puts(" | paused="); dbg_put_u32((uint32_t)g_paused);
#endif
            dbg_puts("\r\n");
        }

#if DEMO_STEP >= 3 && UNO_RAW_DUMP
        /* 原始字节抓包（排障用，UNO_RAW_DUMP=1 时启用）：
             正确（Example-1 @57600）应能看到 0A FA 0A 00 02 ... 00 0B 周期性出现；
             一串乱码  -> 波特率不对；
             一直不打印 -> 一个字节都没进来（接线/共地/UNO 没在发）。 */
        if (g_uno_rx_bytes != last_uno_rx)
        {
            uint8_t buf[16];
            uint8_t k;

            last_uno_rx = g_uno_rx_bytes;
            UNO_Snapshot(buf, 16);
            dbg_puts("UNO raw: ");
            for (k = 0; k < 16; k++)
            {
                dbg_put_hex8(buf[k]);
                dbg_putc(' ');
            }
            dbg_puts("\r\n");
        }
#endif

#if DEMO_STEP >= 2
        HMI_Poll();   /* 处理屏上行按键通知（按钮等） */
#endif

#if DEMO_STEP == 2
        /* 阶段2：写死值上屏测试，用来确认“MCU -> 屏”这条链路。
           这里刻意改用【最基本的单条写文本】指令 EE B1 10 <screen> <ctrl> <str> FF FC FF FF，
           不使用批量(B1 12)和刷屏开关(B3)，以便把变量降到最少：
             - 若屏能显示值   -> 链路通，问题只可能在批量/刷屏指令上；
             - 若屏仍无值     -> 问题在物理链路或屏工程本身（见回显）。
           同时每拍把计数回显到 USART1，用来证明“MCU 确实在跑阶段2并在发”。 */

        /* 每 2s 发一次“查询当前画面”EE B1 01（血压工程验证过：屏会应答），
           并回显从屏收到的累计字节数 g_hmi_rx_bytes：
             - 数字在涨  -> 屏有应答 -> 接线 + 波特率 + 帧格式三项都通；
             - 一直是 0  -> 屏没回   -> 先怀疑波特率（改 9600 再试）或接线。 */
        if (millis() - t_query >= 2000)
        {
            t_query = millis();
            HMI_GetScreen();
            dbg_puts("S2 query B1 01 @");
            dbg_put_u32(g_screen_baud);
            dbg_puts("  screen_rx_bytes=");
            dbg_put_u32(g_hmi_rx_bytes);
            if (g_hmi_rx_bytes != last_rx) dbg_puts("  <== SCREEN ALIVE!");
            dbg_puts("\r\n");
            last_rx = g_hmi_rx_bytes;
        }

        if (millis() - t_demo >= 400)
        {
            t_demo = millis();
            s2_n++;

            HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2,     "98");
            HMI_SetText(SCR_MAIN, MAIN_TXT_PR,       "72");
            HMI_SetText(SCR_MAIN, MAIN_TXT_QUALITY,  "95");
            HMI_SetText(SCR_MAIN, MAIN_TXT_VALID,    "OK");
            HMI_SetText(SCR_MAIN, MAIN_TXT_DURATION, "T 00:00:05");
            HMI_SetText(SCR_MAIN, MAIN_TXT_SPO2_LIM, "90-100 %");

            dbg_puts("S2 push ");
            dbg_put_u32(s2_n);
            dbg_puts(" -> spo2=98 pr=72\r\n");
        }
#endif

#if DEMO_STEP >= 4
        if (g_new_frame)
        {
            g_new_frame = 0;
            on_uno_frame();   /* 推一个 PPG 波形点（约 90 帧/s；暂停时内部忽略） */
        }
        task_hmi_value();     /* 400ms 数值刷新 + 报警判定 */
        task_hmi_trend();     /* 2s 趋势追加一点（2026-10-10 由 60s 改） */
        task_clock();         /* 软 RTC：每秒推进 + 周期同步屏时间 */
        g_meas.monitor_sec = millis() / 1000;
#endif
    }
}
