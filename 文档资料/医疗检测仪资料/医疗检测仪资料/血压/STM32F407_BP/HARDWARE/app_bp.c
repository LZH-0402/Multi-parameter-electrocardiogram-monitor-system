/* ============================================================================
 * app_bp.c — 血压测量应用主逻辑（HKB-08B+ 传感器 → 大彩串口屏，STM32F407 + SPL + Keil AC5）
 * ----------------------------------------------------------------------------
 * 一、三路串口分工
 *   USART1(PA9/PA10)  调试口 115200 → PC 串口助手，只看不控，打印 RX1/RX2/RX3 与状态。
 *   USART3(PB10/PB11) 屏 9600 → 大彩组态屏，发指令 / 收按钮事件。
 *   UART5(PC12/PD2)   传感器 115200（TTL） → HKB-08B+ 血压模块，组帧 / 解帧。
 *
 * 二、屏页面与控件（BP_Hospital.tftprj，无 boot 页，页号 0~9）
 *   0=idle 1=measuring 2=result 3=notice 4=history 5=settings
 *   6=maintenance 7=stopping 8=confirm 9=time
 *   公共控件：100=CLOCK 101=CONTEXT 102=FOOTER 103=TIP_TITLE 104=TIP_DETAIL
 *   关键控件：结果页 110=SYS 111=DIA 112=PR；结果提示 103/104；历史页 110~133(4行×6列)+140页标。
 *
 * 三、状态机（测量生命周期）
 *   ST_IDLE → ST_MEASURING → ST_STOPPING → ST_IDLE
 *   由 s_state 记录；stop_measure 后若 1.5s 内没收到传感器 0xA1 应答，app_loop 强制回 IDLE。
 *
 * 四、正常 / 异常分级（步骤7，落地点见 bp_classify / bp_pr_note / result_show）
 *   收缩压 <90 偏低 / 90~139 正常 / 140~159 偏高(1级) / ≥160 高血压(2级)
 *   舒张压 <60 偏低 / 60~89 正常 / 90~99 偏高(1级) / ≥100 高血压(2级)
 *   脉率 <60 偏慢 / 60~100 正常 / >100 偏快（仅提示，不计入血压分级）
 *
 * 五、代码执行主流程（详见《血压代码编程思路与执行流程.md》）
 *   上电 → main() → app_init()（GPIO/NVIC/DMA/UART/屏/传感器初始化）
 *        → while(1) app_loop()：
 *           1) 把环形缓冲里的屏字节(s_q2)送 hmi_rx_feed 组帧，按钮事件在 hmi_rx_frame 分发；
 *           2) 把传感器字节(s_q3)送 bp_sensor_feed 解帧，解出整帧回调 on_bp_frame 处理；
 *           3) 每 1s 软 RTC(clock_tick_1s) 刷新时钟 / 测量计时。
 *   字节来源：USART3/UART5 的 IDLE 中断 → on_uart_rx_idle → 把 DMA 缓冲字节搬进环形缓冲(s_q2/s_q3)。
 *   字节来源(回退)：RXNE 中断 → on_uart_rx 直接入环形缓冲。
 * ==========================================================================*/
#include "stm32f4xx.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_dma.h"
#include "misc.h"
#include <string.h>
#include "bp_sensor.h"
#include "hmi_dacai.h"
#include "app_bp.h"

extern uint32_t SystemCoreClock;

#define DEBUG_UART  USART1   
   #define SCREEN_UART USART3   
   #define SENSOR_UART UART5   
#define HMI_SCREEN_IDLE        0u    
    #define HMI_SCREEN_MEASURING   1u    
    #define HMI_SCREEN_RESULT      2u    
    #define HMI_SCREEN_NOTICE      3u    
    #define HMI_SCREEN_HISTORY     4u    
    #define HMI_SCREEN_SETTINGS    5u
#define HMI_SCREEN_MAINTENANCE 6u
#define HMI_SCREEN_STOPPING    7u    
    #define HMI_SCREEN_CONFIRM     8u
#define HMI_SCREEN_TIME        9u    
#define HMI_CTRL_CLOCK         100u  
  #define HMI_CTRL_CONTEXT       101u
#define HMI_CTRL_FOOTER        102u
#define HMI_CTRL_TIP_TITLE     103u
#define HMI_CTRL_TIP_DETAIL    104u

#define HMI_CTRL_IDLE_LAST_RESULT  110u  
  #define HMI_CTRL_IDLE_LAST_INFO    111u  
  #define HMI_CTRL_MEAS_STATUS       110u  
  #define HMI_CTRL_MEAS_PRESSURE     111u  
  #define HMI_CTRL_MEAS_ELAPSED      112u  
  #define HMI_CTRL_RESULT_SYS        110u  
  #define HMI_CTRL_RESULT_DIA        111u  
  #define HMI_CTRL_RESULT_PR         112u  
  #define HMI_CTRL_NOTICE_ERRCODE    110u  
  #define HMI_CTRL_NOTICE_TITLE      111u  
  #define HMI_CTRL_NOTICE_LINE1      112u  
  #define HMI_CTRL_NOTICE_LINE2      113u  
  #define HMI_CTRL_NOTICE_VALIDITY   114u  
  #define HMI_CTRL_STOP_STATUS       110u  
  #define HMI_CTRL_STOP_PRESSURE     111u  
  #define HMI_CTRL_STOP_DETAIL       112u  
  #define HMI_CTRL_HIST_PAGE_INDEX   140u  
  #define HMI_CTRL_HIST_ROW0         110u  
  #define HMI_CTRL_HIST_COLS         6u

#define ST_TXT_CONTEXT_WAIT    "\xC9\xE8\xB1\xB8\xC1\xAC\xBD\xD3\xD6\xD0"        
        #define ST_TXT_CONTEXT_IDLE    "\xCE\xB4\xB9\xD8\xC1\xAA\xBB\xBC\xD5\xDF"        
        #define ST_TXT_CONTEXT_MEAS    "\xD5\xFD\xD4\xDA\xB2\xE2\xC1\xBF"                
                #define ST_TXT_CONTEXT_STOP    "\xD5\xFD\xD4\xDA\xCD\xA3\xD6\xB9"                
                #define ST_TXT_CONTEXT_DONE    "\xB2\xE2\xC1\xBF\xCD\xEA\xB3\xC9"                
                #define ST_TXT_FOOTER_WAIT     "\xB5\xC8\xB4\xFD\xC9\xE8\xB1\xB8\xD7\xB4\xCC\xAC"
#define ST_TXT_FOOTER_OK       "\xC9\xE8\xB1\xB8\xD2\xD1\xBE\xCD\xD0\xF7"        
        #define ST_TXT_FOOTER_MEAS     "\xC6\xF8\xD1\xB9\xB2\xE2\xC1\xBF\xD6\xD0"        
        #define ST_TXT_FOOTER_STOP     "\xD5\xFD\xD4\xDA\xCD\xA3\xD6\xB9\xB2\xD9\xD7\xF7"
#define ST_TXT_TIP_WAIT        "\xB5\xC8\xB4\xFD\xC9\xE8\xB1\xB8\xD7\xB4\xCC\xAC"
#define ST_TXT_TIP_OK          "\xC9\xE8\xB1\xB8\xBE\xCD\xD0\xF7\xA3\xAC\xBF\xC9\xBF\xAA\xCA\xBC\xB2\xE2\xC1\xBF" 
 #define ST_TXT_TIP_MEAS        "\xD5\xFD\xD4\xDA\xB2\xE2\xC1\xBF\xA3\xAC\xC7\xEB\xB1\xA3\xB3\xD6\xB0\xB2\xBE\xB2" 
 #define ST_TXT_TIP_STOP        "\xD5\xFD\xD4\xDA\xCD\xA3\xD6\xB9\xA3\xAC\xC7\xEB\xC9\xD4\xB5\xC8" 
 #define ST_TXT_TIP_DETAIL_MEAS "\xD2\xD1\xD3\xC3\xCA\xB1\xCB\xE6\xD6\xD0\xB9\xE6\xD7\xD4\xB6\xAF\xCD\xAC\xB2\xBD\xA3\xAC\xC7\xEB\xB2\xBB\xD2\xAA\xB6\xAF\xCA\xD6\xB1\xDB" 
 #define ST_TXT_TIP_DETAIL_STOP "\xC6\xF8\xB7\xA7\xD5\xFD\xD4\xDA\xB7\xC5\xC6\xF8\xA3\xAC\xC7\xEB\xC9\xD4\xB5\xC8"                                                  
#define HMI_BTN_IDLE_START      10u
#define HMI_BTN_IDLE_HISTORY    11u
#define HMI_BTN_IDLE_SETTINGS   12u
#define HMI_BTN_MEAS_STOP       10u
#define HMI_BTN_RESULT_REMEAS   10u
#define HMI_BTN_RESULT_HISTORY  11u
#define HMI_BTN_RESULT_NEWPAT   12u
#define HMI_BTN_NOTICE_RETRY    10u
#define HMI_BTN_NOTICE_BACK     11u
#define HMI_BTN_HIST_PREV       10u
#define HMI_BTN_HIST_NEXT       11u
#define HMI_BTN_HIST_BACK       12u
#define HMI_BTN_STOPPING_STOP   10u

#define HMI_BTN_SET_TIME        12u   
   #define HMI_CTRL_SETTINGS_TIME  111u  
  #define HMI_CTRL_SETTINGS_BRIGHT 110u 
 #define HMI_BTN_BRIGHT_MINUS    10u   
   #define HMI_BTN_BRIGHT_PLUS     11u   
   #define BRIGHT_MIN_PERCENT      20u   
   #define BRIGHT_MAX_PERCENT      100u  
  #define BRIGHT_STEP             20u   
   #define BRIGHT_DEFAULT_PERCENT  100u  
  #define HMI_CTRL_TIME_YEAR      110u
#define HMI_CTRL_TIME_MONTH     111u
#define HMI_CTRL_TIME_DAY       112u
#define HMI_CTRL_TIME_HOUR      113u
#define HMI_CTRL_TIME_MINUTE    114u
#define HMI_CTRL_TIME_EDITFIELD 120u  
  #define HMI_CTRL_TIME_EDITHELP  121u  
  #define HMI_BTN_TIME_FIELD0     10u   
   #define HMI_BTN_TIME_ADD        15u
#define HMI_BTN_TIME_SUB        16u
#define HMI_BTN_TIME_CANCEL     17u
#define HMI_BTN_TIME_SAVE       18u

#define HIST_CAP 100u

volatile uint32_t g_tick = 0;
uint32_t get_tick(void) { return g_tick; }

static void on_bp_frame(uint8_t cmd, const uint8_t *data, uint8_t len);
static void bp_send_cmd(uint8_t cmd, const uint8_t *data, uint8_t data_len);
static void bp_start_measure(void);  static void dbg_hex2(uint8_t b);   
static volatile uint32_t s_rx2_cnt = 0;
static volatile uint32_t s_rx3_cnt = 0;

#define RXQ_SIZE 256
static volatile uint8_t  s_q3[RXQ_SIZE];
static volatile uint16_t s_q3_head = 0, s_q3_tail = 0;
static volatile uint8_t  s_q2[RXQ_SIZE];
static volatile uint16_t s_q2_head = 0, s_q2_tail = 0;

#if RX_USE_DMA_IDLE
#define DMA_RX_BUF_SIZE 64
static uint8_t s_dma_screen_buf[DMA_RX_BUF_SIZE];   static uint8_t s_dma_sensor_buf[DMA_RX_BUF_SIZE];   
   #endif


typedef enum {
    ST_IDLE = 0,          ST_MEASURING,         ST_STOPPING       } bp_state_t;
static bp_state_t s_state = ST_IDLE;

static uint32_t s_rtc_base_tick = 0;   static uint32_t s_rtc_base_sec  = 0;   static uint16_t s_year = 2026;
static uint8_t  s_month = 1, s_day = 1;
static uint32_t s_sec_of_day = 0;      /* 0~86399 */

static uint32_t s_meas_start_tick = 0;
static uint16_t s_elapsed_sec = 0;

typedef struct {
    uint8_t  valid;            uint16_t sys, dia;
    uint8_t  pr;
    uint8_t  irregular;
    uint16_t year; uint8_t mon, day, hour, min;   } bp_last_t;
static bp_last_t s_last;

typedef struct {
    uint8_t  valid;            uint16_t sys, dia;
    uint8_t  pr;
    uint16_t year; uint8_t mon, day, hour, min;
    uint8_t  state;        } hist_rec_t;
static hist_rec_t s_hist[HIST_CAP];
static uint16_t s_hist_count = 0;   static uint16_t s_hist_head = 0;    static uint8_t  s_hist_page = 0;    
static uint8_t  s_tedit_field = 0;  static uint16_t s_tedit_year = 2026;
static uint8_t  s_tedit_mon = 1, s_tedit_day = 1, s_tedit_hour = 0, s_tedit_min = 0;

static uint8_t  s_bright_percent = BRIGHT_DEFAULT_PERCENT;

static uint32_t s_stop_tick  = 0;    static uint8_t  s_stop_acked = 0;    
static void clock_tick_1s(void);
static void clock_broadcast(void);
static void hist_add(uint8_t valid, uint16_t sys, uint16_t dia, uint8_t pr,
                     uint8_t state, uint16_t yy, uint8_t mm, uint8_t dd,
                     uint8_t hh, uint8_t mi);
static void hist_show_page(void);
static void stop_measure(void);
static void result_show(void);
static void notice_show(uint8_t err_code);
static void time_edit_begin(void);       static void time_edit_render(void);      static void time_edit_step(int8_t dir);  static void time_edit_commit(void);      static void settings_show_time(void);    static void bright_apply(void);          static void bright_show(void);           static void bright_step(int8_t dir);     static void status_refresh(void);        static void status_set(const char *ctx, const char *foot, const char *tip, const char *det); static void idle_refresh(void);          
static void delay_ms(uint32_t ms)
{
    uint32_t t0 = g_tick;
    while (g_tick - t0 < ms) {}
}

static void uart_send_byte(USART_TypeDef *u, uint8_t b)
{
    while (USART_GetFlagStatus(u, USART_FLAG_TXE) == RESET);
    USART_SendData(u, b);
}
static void uart_send_buf(USART_TypeDef *u, const uint8_t *p, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) uart_send_byte(u, p[i]);
}

static void dbg_str(const char *s)
{
    uart_send_buf(DEBUG_UART, (const uint8_t *)s, (uint16_t)strlen(s));
}
static void dbg_u32(uint32_t v)
{
    char t[12]; int i = 0;
    if (v == 0) t[i++] = '0';
    else { while (v) { t[i++] = (char)('0' + v % 10); v /= 10; } }
    while (i > 0) uart_send_byte(DEBUG_UART, (uint8_t)t[--i]);
}

static uint8_t u32_dec(uint32_t v, char *buf)
{
    char t[12]; uint8_t i = 0;
    if (v == 0) t[i++] = '0';
    else { while (v) { t[i++] = (char)('0' + v % 10); v /= 10; } }
    uint8_t n = i;
    while (i > 0) *buf++ = t[--i];
    *buf = '\0';
    return n;
}

static void hmi_send_cb(const uint8_t *buf, uint16_t len)
{
    uart_send_buf(SCREEN_UART, buf, len);
}

/*
 * 上电自检/空闲页诊断：切到 idle 页 → 刷新“上次结果” → 刷新公共状态条 →
 * 发“查询当前页”(EE B1 01) 让屏应答，借此一次性验证 接线+波特率+帧格式 三项是否正确。
 */
static void screen_diag_on_idle(void)
{
    hmi_change_page(HMI_SCREEN_IDLE);                                  idle_refresh();                                                    status_refresh();                                                  hmi_get_screen();                             dbg_str("IDLE(0) + LAST_RESULT/INFO + STATUS + GetScreen\r\n");
}

#define SCR_BUF_SIZE 32
static uint8_t  s_scr_buf[SCR_BUF_SIZE];
static uint8_t  s_scr_len = 0;
static uint8_t  s_scr_in_frame = 0;

/*
 * 屏帧解析 + 按钮分发：只处理大彩 B1 子指令。
 *    0x01=查询当前页应答(仅打印)；
 *    0x11=按钮返回帧：只有“按下”(status=1)才处理，按 页面+控件ID 分发到对应业务
 *    （开始测量/历史/设置/停止/时间编辑等）。
 * 注意：屏“切页型”按钮由屏自己跳页，MCU 只做业务；瞬变型按钮则需 MCU 主动切页。
 */
static void hmi_rx_frame(const uint8_t *f, uint8_t len)
{
    uint8_t sub;
    if (len < 5) return;
    if (f[1] != 0xB1) return;                sub = f[2];
    if (sub == 0x01) {                           uint16_t scr = ((uint16_t)f[3] << 8) | f[4];
        dbg_str(">>> SCREEN GET screen=");
        dbg_u32(scr);
        dbg_str("\r\n");
    } else if (sub == 0x11) {                    uint16_t scr, ctrl;
        uint8_t  ctype, subtype, status;
        if (len < 9) return;
        scr  = ((uint16_t)f[3] << 8) | f[4];
        ctrl = ((uint16_t)f[5] << 8) | f[6];
        ctype   = f[7];                          subtype = f[8];
        status  = (len > 9) ? f[9] : 0;          dbg_str(">>> SCREEN BTN screen=");
        dbg_u32(scr);
        dbg_str(" ctrl=");
        dbg_u32(ctrl);
        dbg_str(" type=");
        dbg_hex2(ctype);
        dbg_str(" sub=");
        dbg_hex2(subtype);
        dbg_str(" st=");
        dbg_hex2(status);
        dbg_str("\r\n");

                if (status != 1u) return;

                if (scr == HMI_SCREEN_IDLE) {
            if (ctrl == HMI_BTN_IDLE_START) {
                                bp_start_measure();
            } else if (ctrl == HMI_BTN_IDLE_HISTORY) {
                s_hist_page = 0;
                hist_show_page();                        } else if (ctrl == HMI_BTN_IDLE_SETTINGS) {
                settings_show_time();                        bright_show();                           }
                    }
                else if (scr == HMI_SCREEN_MEASURING) {
            if (ctrl == HMI_BTN_MEAS_STOP) {
                stop_measure();
            }
        }
                else if (scr == HMI_SCREEN_STOPPING) {
            if (ctrl == HMI_BTN_STOPPING_STOP) {
                                if (s_state == ST_MEASURING || s_state == ST_STOPPING) stop_measure();
            }
        }
                else if (scr == HMI_SCREEN_RESULT) {
            if (ctrl == HMI_BTN_RESULT_REMEAS) {
                bp_start_measure();                      } else if (ctrl == HMI_BTN_RESULT_HISTORY) {
                s_hist_page = 0;
                hist_show_page();                        }
                    }
                else if (scr == HMI_SCREEN_NOTICE) {
            if (ctrl == HMI_BTN_NOTICE_RETRY) {
                bp_start_measure();                      }
                    }
                else if (scr == HMI_SCREEN_HISTORY) {
            uint16_t total_pages = (uint16_t)((s_hist_count + 3u) / 4u);
            if (total_pages == 0u) total_pages = 1u;
            if (ctrl == HMI_BTN_HIST_PREV) {
                if (s_hist_page > 0u) { s_hist_page--; hist_show_page(); }
            } else if (ctrl == HMI_BTN_HIST_NEXT) {
                if ((uint16_t)(s_hist_page + 1u) < total_pages) { s_hist_page++; hist_show_page(); }
            }
                    }
                else if (scr == HMI_SCREEN_CONFIRM) {
                        if (ctrl == 11u) {
                                s_last.valid = 0;
                idle_refresh();                          status_refresh();                    }
        }
                else if (scr == HMI_SCREEN_SETTINGS) {
            if (ctrl == HMI_BTN_SET_TIME) {
                time_edit_begin();
            } else if (ctrl == HMI_BTN_BRIGHT_MINUS) {
                bright_step(-1);                         } else if (ctrl == HMI_BTN_BRIGHT_PLUS) {
                bright_step(+1);                         }
        }
                else if (scr == HMI_SCREEN_TIME) {
            if (ctrl >= HMI_BTN_TIME_FIELD0 && ctrl <= (uint16_t)(HMI_BTN_TIME_FIELD0 + 4u)) {
                s_tedit_field = (uint8_t)(ctrl - HMI_BTN_TIME_FIELD0);                   time_edit_render();
            } else if (ctrl == HMI_BTN_TIME_ADD) {
                time_edit_step(+1);
            } else if (ctrl == HMI_BTN_TIME_SUB) {
                time_edit_step(-1);
            } else if (ctrl == HMI_BTN_TIME_SAVE) {
                time_edit_commit();                 }
                    }
    }
}

/*
 * 屏字节“组帧器”：逐字节喂入，遇 0xEE 开头、以 FF FC FF FF 结尾即收完一帧，
 * 交给 hmi_rx_frame 解按钮事件。缓冲满或超长则丢弃重来。
 */
static void hmi_rx_feed(uint8_t b)
{
    if (!s_scr_in_frame) {
        if (b == 0xEE) {                             s_scr_len = 0;
            s_scr_buf[s_scr_len++] = b;
            s_scr_in_frame = 1;
        }
        return;
    }
    if (s_scr_len >= SCR_BUF_SIZE) {             s_scr_in_frame = 0; s_scr_len = 0; return;
    }
    s_scr_buf[s_scr_len++] = b;
        if (s_scr_len >= 4 &&
        s_scr_buf[s_scr_len-4] == 0xFF && s_scr_buf[s_scr_len-3] == 0xFC &&
        s_scr_buf[s_scr_len-2] == 0xFF && s_scr_buf[s_scr_len-1] == 0xFF) {
        hmi_rx_frame(s_scr_buf, s_scr_len);
        s_scr_in_frame = 0; s_scr_len = 0;
    }
}


static uint8_t str_app(char *buf, uint8_t pos, const char *s)
{
    while (*s) buf[pos++] = *s++;
    return pos;
}
static uint8_t str_app2(char *buf, uint8_t pos, uint8_t v)
{
    buf[pos++] = (char)('0' + (v / 10) % 10);
    buf[pos++] = (char)('0' + v % 10);
    return pos;
}
static uint8_t str_app4(char *buf, uint8_t pos, uint16_t v)
{
    buf[pos++] = (char)('0' + (v / 1000) % 10);
    buf[pos++] = (char)('0' + (v / 100) % 10);
    buf[pos++] = (char)('0' + (v / 10) % 10);
    buf[pos++] = (char)('0' + v % 10);
    return pos;
}

/*
 * 软 RTC 每 1s 推进：用 SysTick 的 g_tick 与基准求已过秒数，换算年月日时分秒；
 * 满整天才进位日期（闰年 2 月 29 天），并重置基准避免长累计误差。
 */
static void clock_tick_1s(void)
{
    uint32_t elapsed = (g_tick - s_rtc_base_tick) / 1000u;
    uint32_t sec = s_rtc_base_sec + elapsed;
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t days;

    s_sec_of_day = sec % 86400u;
    days = sec / 86400u;
    if (days == 0) return;
    s_rtc_base_sec += days * 86400u;
    s_rtc_base_tick = g_tick;                    while (days--) {
        uint8_t feb = ((s_year % 4u == 0u && s_year % 100u != 0u) || s_year % 400u == 0u) ? 29u : 28u;
        uint8_t dmax = (s_month == 2u) ? feb : dim[s_month - 1u];
        s_day++;
        if (s_day > dmax) { s_day = 1; s_month++; if (s_month > 12u) { s_month = 1; s_year++; } }
    }
}

static void clock_broadcast(void)
{
    char b[20];
    uint8_t k = 0, hh, mm, sc;
    hh = (uint8_t)(s_sec_of_day / 3600u);
    mm = (uint8_t)((s_sec_of_day / 60u) % 60u);
    sc = (uint8_t)(s_sec_of_day % 60u);
    k = str_app4(b, k, s_year); b[k++] = '-';
    k = str_app2(b, k, s_month); b[k++] = '-';
    k = str_app2(b, k, s_day);   b[k++] = ' ';
    k = str_app2(b, k, hh);      b[k++] = ':';
    k = str_app2(b, k, mm);      b[k++] = ':';
    k = str_app2(b, k, sc);
    b[k] = '\0';
        {
        uint16_t pg;
        for (pg = 0; pg <= HMI_SCREEN_TIME; pg++) hmi_write_text(pg, HMI_CTRL_CLOCK, b);
    }
}

#define ST_PUBLIC_MASK  0x01u   
   #define ST_TIP_MASK     0x02u   
   #define ST_TIP_SKIP_PAGE HMI_SCREEN_RESULT   
/*
 * 公共状态条写入：把 上下文/页脚/提示标题/提示详情 写到所有页对应控件(100~104)。
 * settings/maintenance/confirm 三页无这些控件故跳过；result 页的 TIP 单独由 result_show 写。
 */
static void status_set(const char *ctx, const char *foot, const char *tip, const char *det)
{
    uint16_t pg;
    for (pg = 0; pg <= HMI_SCREEN_TIME; pg++) {
        if (pg == HMI_SCREEN_SETTINGS || pg == HMI_SCREEN_MAINTENANCE ||
            pg == HMI_SCREEN_CONFIRM) continue;              if (ctx)  hmi_write_text(pg, HMI_CTRL_CONTEXT,  ctx);
        if (foot) hmi_write_text(pg, HMI_CTRL_FOOTER,   foot);
        if (pg == ST_TIP_SKIP_PAGE) continue;                if (tip)  hmi_write_text(pg, HMI_CTRL_TIP_TITLE,  tip);
        if (det)  hmi_write_text(pg, HMI_CTRL_TIP_DETAIL, det);
    }
}

/*
 * 空闲页刷新：把 s_last（上次有效结果）写到 idle 页“上次结果(110)/上次信息(111)”；
 * 无有效结果时显示 “-- / --” 与“尚未测量”。
 */
static void idle_refresh(void)
{
    char b[24];
    uint8_t k;
    if (s_last.valid) {
        k = 0;
        k += u32_dec(s_last.sys, b + k); b[k++] = '/';
        k += u32_dec(s_last.dia, b + k); b[k] = '\0';
        hmi_write_text(HMI_SCREEN_IDLE, HMI_CTRL_IDLE_LAST_RESULT, b);
        k = 0; k = str_app(b, k, "PR=");
        k += u32_dec(s_last.pr, b + k); b[k] = '\0';
        hmi_write_text(HMI_SCREEN_IDLE, HMI_CTRL_IDLE_LAST_INFO, b);
    } else {
        hmi_write_text(HMI_SCREEN_IDLE, HMI_CTRL_IDLE_LAST_RESULT, "-- / --");
        hmi_write_text(HMI_SCREEN_IDLE, HMI_CTRL_IDLE_LAST_INFO, "\xC9\xD0\xCE\xDE\xB2\xE2\xC1\xBF\xBD\xE1\xB9\xFB");
    }
}

/*
 * 状态刷新：按当前 s_state 用 status_set 把 CONTEXT/FOOTER/TIP 公共状态条写到所有页
 *    （settings/maintenance/confirm 无此控件则跳过；result 页的 TIP 由 result_show 单独写）。
 */
static void status_refresh(void)
{
    switch (s_state) {
    case ST_MEASURING:
        status_set(ST_TXT_CONTEXT_MEAS, ST_TXT_FOOTER_MEAS, ST_TXT_TIP_MEAS, ST_TXT_TIP_DETAIL_MEAS);
        break;
    case ST_STOPPING:
        status_set(ST_TXT_CONTEXT_STOP, ST_TXT_FOOTER_STOP, ST_TXT_TIP_STOP, ST_TXT_TIP_DETAIL_STOP);
        break;
    default:  /* ST_IDLE */
        if (s_last.valid) status_set(ST_TXT_CONTEXT_DONE, ST_TXT_FOOTER_OK, ST_TXT_TIP_OK, "");
        else              status_set(ST_TXT_CONTEXT_IDLE, ST_TXT_FOOTER_WAIT, ST_TXT_TIP_WAIT, "");
        break;
    }
    dbg_str(">>> STATUS refresh state=");
    dbg_u32((uint32_t)s_state);
    dbg_str("\r\n");
}


static uint8_t month_days(uint16_t y, uint8_t m)
{
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2u) {
        uint8_t feb = ((y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u) ? 29u : 28u;
        return feb;
    }
    if (m < 1u || m > 12u) return 31u;
    return dim[m - 1u];
}

/*
 * 进入时间编辑：把当前软 RTC 的年/月/日/时/分 拷到编辑缓冲(s_tedit_*)，字段置 0，刷新 time 页。
 */
static void time_edit_begin(void)
{
    s_tedit_year = s_year;
    s_tedit_mon  = s_month;
    s_tedit_day  = s_day;
    s_tedit_hour = (uint8_t)(s_sec_of_day / 3600u);
    s_tedit_min  = (uint8_t)((s_sec_of_day / 60u) % 60u);
    s_tedit_field = 0u;
    time_edit_render();
    dbg_str(">>> TIME EDIT begin\r\n");
}

static void time_edit_render(void)
{
    char b[8];
    uint8_t k;
    const char *field_name;

    k = str_app4(b, 0, s_tedit_year); b[k] = '\0';
    hmi_write_text(HMI_SCREEN_TIME, HMI_CTRL_TIME_YEAR, b);
    k = str_app2(b, 0, s_tedit_mon);  b[k] = '\0';
    hmi_write_text(HMI_SCREEN_TIME, HMI_CTRL_TIME_MONTH, b);
    k = str_app2(b, 0, s_tedit_day);  b[k] = '\0';
    hmi_write_text(HMI_SCREEN_TIME, HMI_CTRL_TIME_DAY, b);
    k = str_app2(b, 0, s_tedit_hour); b[k] = '\0';
    hmi_write_text(HMI_SCREEN_TIME, HMI_CTRL_TIME_HOUR, b);
    k = str_app2(b, 0, s_tedit_min);  b[k] = '\0';
    hmi_write_text(HMI_SCREEN_TIME, HMI_CTRL_TIME_MINUTE, b);

    switch (s_tedit_field) {
        case 0u:  field_name = "\xC4\xEA";       break;           case 1u:  field_name = "\xD4\xC2";       break;           case 2u:  field_name = "\xC8\xD5";       break;           case 3u:  field_name = "\xCA\xB1";       break;           default:  field_name = "\xB7\xD6";       break;       }
        hmi_write_text(HMI_SCREEN_TIME, HMI_CTRL_TIME_EDITFIELD, field_name);
    hmi_write_text(HMI_SCREEN_TIME, HMI_CTRL_TIME_EDITHELP,
                   "\xD1\xA1\xD7\xD6\xB6\xCE\xBA\xF3\xB0\xB4 +/-\xA3\xAC\xB1\xA3\xB4\xE6\xBA\xF3\xC9\xFA\xD0\xA7"); }

static void time_edit_step(int8_t dir)
{
    switch (s_tedit_field) {
        case 0u: {               if (dir > 0) s_tedit_year = (uint16_t)((s_tedit_year >= 2099u) ? 2000u : (s_tedit_year + 1u));
            else         s_tedit_year = (uint16_t)((s_tedit_year <= 2000u) ? 2099u : (s_tedit_year - 1u));
            break;
        }
        case 1u: {               if (dir > 0) s_tedit_mon = (uint8_t)((s_tedit_mon >= 12u) ? 1u : (s_tedit_mon + 1u));
            else         s_tedit_mon = (uint8_t)((s_tedit_mon <= 1u)  ? 12u : (s_tedit_mon - 1u));
            break;
        }
        case 2u: {               uint8_t dmax = month_days(s_tedit_year, s_tedit_mon);
            if (dir > 0) s_tedit_day = (uint8_t)((s_tedit_day >= dmax) ? 1u : (s_tedit_day + 1u));
            else         s_tedit_day = (uint8_t)((s_tedit_day <= 1u)    ? dmax : (s_tedit_day - 1u));
            break;
        }
        case 3u: {               if (dir > 0) s_tedit_hour = (uint8_t)((s_tedit_hour >= 23u) ? 0u : (s_tedit_hour + 1u));
            else         s_tedit_hour = (uint8_t)((s_tedit_hour == 0u)  ? 23u : (s_tedit_hour - 1u));
            break;
        }
        default: {               if (dir > 0) s_tedit_min = (uint8_t)((s_tedit_min >= 59u) ? 0u : (s_tedit_min + 1u));
            else         s_tedit_min = (uint8_t)((s_tedit_min == 0u)  ? 59u : (s_tedit_min - 1u));
            break;
        }
    }
        {
        uint8_t dmax = month_days(s_tedit_year, s_tedit_mon);
        if (s_tedit_day > dmax) s_tedit_day = dmax;
    }
    time_edit_render();
}

static void settings_show_time(void)
{
    char b[20];
    uint8_t k = 0, hh, mm, sc;
    hh = (uint8_t)(s_sec_of_day / 3600u);
    mm = (uint8_t)((s_sec_of_day / 60u) % 60u);
    sc = (uint8_t)(s_sec_of_day % 60u);
    k = str_app4(b, k, s_year); b[k++] = '-';
    k = str_app2(b, k, s_month); b[k++] = '-';
    k = str_app2(b, k, s_day);   b[k++] = ' ';
    k = str_app2(b, k, hh);      b[k++] = ':';
    k = str_app2(b, k, mm);      b[k++] = ':';
    k = str_app2(b, k, sc);
    b[k] = '\0';
    hmi_write_text(HMI_SCREEN_SETTINGS, HMI_CTRL_SETTINGS_TIME, b);
}

#define HMI_BACKLIGHT_MAP(p)  (uint8_t)(255u - ((uint32_t)(p) * 255u / 100u))

static void bright_apply(void)
{
    hmi_set_backlight(HMI_BACKLIGHT_MAP(s_bright_percent));
}

static void bright_show(void)
{
    char b[6];
    uint8_t k = 0;
    if (s_bright_percent >= 100u) { b[k++] = '1'; b[k++] = '0'; b[k++] = '0'; }
    else if (s_bright_percent >= 10u) { b[k++] = (char)('0' + s_bright_percent / 10u); b[k++] = (char)('0' + s_bright_percent % 10u); }
    else { b[k++] = (char)('0' + s_bright_percent); }
    b[k++] = '%';
    b[k] = '\0';
    hmi_write_text(HMI_SCREEN_SETTINGS, HMI_CTRL_SETTINGS_BRIGHT, b);
}

/*
 * 亮度步进：dir=+1/-1 调整 s_bright_percent（步长 20%，限幅 20%~100%），
 * 立即下发背光指令并刷新 settings 页显示。
 */
static void bright_step(int8_t dir)
{
    if (dir > 0) {
        if (s_bright_percent + BRIGHT_STEP > BRIGHT_MAX_PERCENT) s_bright_percent = BRIGHT_MAX_PERCENT;
        else s_bright_percent = (uint8_t)(s_bright_percent + BRIGHT_STEP);
    } else {
        if (s_bright_percent < BRIGHT_MIN_PERCENT + BRIGHT_STEP) s_bright_percent = BRIGHT_MIN_PERCENT;
        else s_bright_percent = (uint8_t)(s_bright_percent - BRIGHT_STEP);
    }
    bright_apply();
    bright_show();
    dbg_str(">>> BRIGHT ");
    dbg_u32(s_bright_percent);
    dbg_str("%\r\n");
}

/*
 * 提交时间编辑：把编辑缓冲写回软 RTC（年/月/日 + 当日秒数），重置基准，
 * 再广播时钟到各页并刷新 settings 页的 DEVICE_TIME。
 */
static void time_edit_commit(void)
{
    s_year  = s_tedit_year;
    s_month = s_tedit_mon;
    s_day   = s_tedit_day;
        s_sec_of_day = (uint32_t)s_tedit_hour * 3600u + (uint32_t)s_tedit_min * 60u;
    s_rtc_base_sec  = s_sec_of_day;
    s_rtc_base_tick = g_tick;
    clock_broadcast();            settings_show_time();         dbg_str(">>> TIME EDIT saved -> ");
    dbg_u32(s_year); dbg_str("-"); dbg_u32(s_month); dbg_str("-"); dbg_u32(s_day);
    dbg_str(" "); dbg_u32(s_tedit_hour); dbg_str(":"); dbg_u32(s_tedit_min);
    dbg_str("\r\n");
}

static void hist_add(uint8_t valid, uint16_t sys, uint16_t dia, uint8_t pr,
                     uint8_t state, uint16_t yy, uint8_t mm, uint8_t dd,
                     uint8_t hh, uint8_t mi)
{
    hist_rec_t *r = &s_hist[s_hist_head];
    r->valid = valid; r->sys = sys; r->dia = dia; r->pr = pr;
    r->year = yy; r->mon = mm; r->day = dd; r->hour = hh; r->min = mi;
    r->state = state;
    s_hist_head = (uint16_t)((s_hist_head + 1u) % HIST_CAP);
    if (s_hist_count < HIST_CAP) s_hist_count++;
    s_hist_page = 0;   }

/* ===== 血压分级阈值与分级函数（步骤7）：结果页与历史页共用，保证两处判定一致 ===== */
#define BP_SYS_LO    90u
#define BP_SYS_HI    139u
#define BP_SYS_H1    140u
#define BP_SYS_H2    160u
#define BP_DIA_LO    60u
#define BP_DIA_HI    89u
#define BP_DIA_H1    90u
#define BP_DIA_H2    100u
#define BP_PR_LO     60u
#define BP_PR_HI     100u

/* 血压分级级别：0=正常 1=偏低 2=偏高(1级) 3=高血压(2级)；取 SYS/DIA 较高档 */
static uint8_t bp_level(uint16_t sys, uint16_t dia)
{
    uint8_t lvl = 0;
    if (sys < BP_SYS_LO || dia < BP_DIA_LO) lvl = 1;
    if (sys >= BP_SYS_H2 || dia >= BP_DIA_H2) lvl = 3;
    else if (sys >= BP_SYS_H1 || dia >= BP_DIA_H1) lvl = 2;
    return lvl;
}

/* 血压分级短标签（历史列表“状态”列用）：与结果页 bp_classify 同源分级，两处一致 */
static const char *bp_classify_tag(uint16_t sys, uint16_t dia)
{
    switch (bp_level(sys, dia)) {
        case 1:  return "\xC6\xAB\xB5\xCD";   /* 偏低 */
        case 2:  return "\xC6\xAB\xB8\xDF";  /* 偏高 */
        case 3:  return "\xB8\xDF\xD1\xAA\xD1\xB9"; /* 高血压 */
        default: return "\xD5\xFD\xB3\xA3";  /* 正常 */
    }
}

/*
 * 历史翻页显示：按当前 s_hist_page 取 4 条(环形缓冲最新在前)写到 history 页 4 行×6 列，
 * 空行清空白，页底写 “n/m” 或“无记录”。
 */
static void hist_show_page(void)
{
    uint16_t total_pages, i;
    char b[16];

    total_pages = (uint16_t)((s_hist_count + 3u) / 4u);
    if (total_pages == 0u) total_pages = 1u;
    if (s_hist_page >= total_pages) s_hist_page = (uint8_t)(total_pages - 1u);

    for (i = 0; i < 4u; i++) {
        uint16_t idx  = (uint16_t)(s_hist_page * 4u + i);
        uint16_t ctrl = (uint16_t)(HMI_CTRL_HIST_ROW0 + i * HMI_CTRL_HIST_COLS);
        if (idx >= s_hist_count) {
            hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 0u, "");
            hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 1u, "");
            hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 2u, "");
            hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 3u, "");
            hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 4u, "");
            hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 5u, "");
            continue;
        }
        {
            uint16_t ai = (uint16_t)((s_hist_head + HIST_CAP - 1u - idx) % HIST_CAP);
            hist_rec_t *r = &s_hist[ai];

            /* 序号 */
            {
                char tmp[8];
                uint8_t n = u32_dec((uint32_t)(idx + 1u), tmp);
                (void)n;
                hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 0u, tmp);
            }

            /* 日期时间 MM-DD HH:MM */
            {
                uint8_t k = 0;
                k = str_app2(b, k, r->mon);  b[k++] = '-';
                k = str_app2(b, k, r->day);  b[k++] = ' ';
                k = str_app2(b, k, r->hour); b[k++] = ':';
                k = str_app2(b, k, r->min);  b[k] = '\0';
                hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 1u, b);
            }

            /* SYS / DIA / PR */
            if (r->valid) {
                char t[8];
                u32_dec(r->sys, t); hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 2u, t);
                u32_dec(r->dia, t); hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 3u, t);
                u32_dec(r->pr,  t); hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 4u, t);
            } else {
                hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 2u, "--");
                hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 3u, "--");
                hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 4u, "--");
            }

            /* 状态列：有效记录显示与结果页一致的血压分级；无效/取消/错误记录保留类别 */
            if (r->valid) {
                /* 有效记录：血压分级短标签(与结果页同源) + 心律不齐标记；state: 0=齐 1=不齐 */
                char st[24];
                uint8_t sk = 0;
                sk = str_app(st, sk, bp_classify_tag(r->sys, r->dia));
                if (r->state == 1u) { sk = str_app(st, sk, "\xA1\xA4\xB2\xBB\xC6\xEB"); }  /* ·不齐 */
                st[sk] = '\0';
                hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 5u, st);
            } else {
                hmi_write_text(HMI_SCREEN_HISTORY, ctrl + 5u,
                    (r->state == 2u) ? "\xD2\xD1\xC8\xA1\xCF\xFB" : "\xB4\xED\xCE\xF3");
            }
        }
    }

    if (s_hist_count == 0u) {
        hmi_write_text(HMI_SCREEN_HISTORY, HMI_CTRL_HIST_PAGE_INDEX, "\xD4\xDD\xCE\xDE\xBC\xC7\xC2\xBC");
    } else {
        uint8_t k = 0;
        k = u32_dec((uint32_t)(s_hist_page + 1u), b); b[k++] = '/';
        k += u32_dec((uint32_t)total_pages, b + k); b[k] = '\0';
        hmi_write_text(HMI_SCREEN_HISTORY, HMI_CTRL_HIST_PAGE_INDEX, b);
    }
}

/*
 * 血压分级（步骤7）：取 收缩压/舒张压 中较高的一级作为总分级，返回 GBK 提示串。
 * 偏低(<90/<60) / 正常 / 偏高1级(140~/90~) / 高血压2级(≥160/≥100)。
 */
static const char *bp_classify(uint16_t sys, uint16_t dia)
{
    switch (bp_level(sys, dia)) {
        case 1:  return "\xD1\xAA\xD1\xB9\xC6\xAB\xB5\xCD\xA3\xAC\xC7\xEB\xB9\xD8\xD7\xA2";   /* 血压偏低，请关注 */
        case 2:  return "\xD1\xAA\xD1\xB9\xC6\xAB\xB8\xDF\xA3\xAC\xC7\xEB\xD7\xA2\xD2\xE2";  /* 血压偏高，请注意 */
        case 3:  return "\xD1\xAA\xD1\xB9\xB9\xFD\xB8\xDF\xA3\xAC\xBD\xA8\xD2\xE9\xBE\xCD\xD2\xBD"; /* 血压过高，建议就医 */
        default: return "\xD1\xAA\xD1\xB9\xD5\xFD\xB3\xA3";  /* 血压正常 */
    }
}

static const char *bp_pr_note(uint8_t pr)
{
    if (pr > BP_PR_HI) return "\xD0\xC4\xC2\xCA\xC6\xAB\xBF\xEC\xA3\xAC\xC7\xEB\xD7\xA2\xD2\xE2";     if (pr < BP_PR_LO) return "\xD0\xC4\xC2\xCA\xC6\xAB\xC2\xFD\xA3\xAC\xC7\xEB\xB9\xD8\xD7\xA2";     return "";
}

/*
 * 结果显示：把 s_last(SYS/DIA/PR) 写到结果页 110/111/112，并刷新 idle 页“上次结果”；
 * 步骤7：TIP_TITLE(103) 写血压分级(bp_classify)，TIP_DETAIL(104) 写
 *    “脉率提示 + 心率不齐提示”合并串（用 str_app 拼接，det[64] 缓冲）。
 */
static void result_show(void)
{
    char t[8];
    char det[64];
    uint8_t k;
    const char *prn;
    u32_dec(s_last.sys, t); hmi_write_text(HMI_SCREEN_RESULT, HMI_CTRL_RESULT_SYS, t);
    u32_dec(s_last.dia, t); hmi_write_text(HMI_SCREEN_RESULT, HMI_CTRL_RESULT_DIA, t);
    u32_dec(s_last.pr,  t); hmi_write_text(HMI_SCREEN_RESULT, HMI_CTRL_RESULT_PR,  t);
    idle_refresh();          
        hmi_write_text(HMI_SCREEN_RESULT, HMI_CTRL_TIP_TITLE, bp_classify(s_last.sys, s_last.dia));

    k = 0;
    prn = bp_pr_note(s_last.pr);
    if (prn[0]) { k = str_app(det, k, prn); }
    if (s_last.irregular) {
        if (k) { det[k++] = 0x3B; det[k++] = 0x20; }          k = str_app(det, k, "\xBD\xDA\xC2\xC9\xCC\xE1\xCA\xBE\xA3\xAC\xC7\xEB\xD3\xC9\xD2\xBD\xBB\xA4\xC8\xCB\xD4\xB1\xB8\xB4\xBA\xCB");
    }
    det[k] = '\0';
    hmi_write_text(HMI_SCREEN_RESULT, HMI_CTRL_TIP_DETAIL, det);
}

/*
 * 异常/通知页：按错误码(0xAD 的 X，或 255=主动取消)写 错误码/标题/说明 到 notice 页。
 */
static void notice_show(uint8_t err_code)
{
    const char *title; const char *line1;
    switch (err_code) {
    case 0: title = "\xCE\xB4\xBC\xEC\xB2\xE2\xB5\xBD\xD3\xD0\xD0\xA7\xC2\xF6\xB2\xAB"; line1 = "\xC7\xEB\xBC\xEC\xB2\xE9\xD0\xE4\xB4\xF8\xCE\xBB\xD6\xC3\xBA\xF3\xD6\xD8\xCA\xD4"; break;
    case 1: title = "\xD1\xB9\xC1\xA6\xCE\xB4\xBD\xA8\xC1\xA2";       line1 = "\xC7\xEB\xBC\xEC\xB2\xE9\xD0\xE4\xB4\xF8\xC5\xE5\xB4\xF7\xBA\xCD\xC6\xF8\xB9\xDC\xC1\xAC\xBD\xD3"; break;
    case 2: title = "\xB1\xBE\xB4\xCE\xBD\xE1\xB9\xFB\xCE\xDE\xD0\xA7";     line1 = "\xC7\xEB\xD6\xD8\xD0\xC2\xB2\xE2\xC1\xBF"; break;
    case 3: title = "\xB3\xAC\xD1\xB9\xB1\xA3\xBB\xA4";         line1 = "\xC7\xEB\xCD\xA3\xD6\xB9\xCA\xB9\xD3\xC3\xB2\xA2\xBC\xEC\xB2\xE9\xC9\xE8\xB1\xB8"; break;
    case 4: title = "\xB2\xE2\xC1\xBF\xB8\xC9\xC8\xC5\xB9\xFD\xB6\xE0";     line1 = "\xC7\xEB\xB1\xA3\xB3\xD6\xBE\xB2\xD6\xB9\xBA\xF3\xD6\xD8\xCA\xD4"; break;
    default: title = "\xB1\xBE\xB4\xCE\xB2\xE2\xC1\xBF\xD2\xD1\xC8\xA1\xCF\xFB";  line1 = "\xCE\xB4\xBB\xF1\xB5\xC3\xD3\xD0\xD0\xA7\xBD\xE1\xB9\xFB"; break;
    }
    { char t[8]; uint8_t k = 0;
      k = str_app(t, k, "E"); k = str_app2(t, k, err_code); t[k] = '\0';
      hmi_write_text(HMI_SCREEN_NOTICE, HMI_CTRL_NOTICE_ERRCODE, t); }
    hmi_write_text(HMI_SCREEN_NOTICE, HMI_CTRL_NOTICE_TITLE, title);
    hmi_write_text(HMI_SCREEN_NOTICE, HMI_CTRL_NOTICE_LINE1, line1);
    hmi_write_text(HMI_SCREEN_NOTICE, HMI_CTRL_NOTICE_LINE2, "");
    hmi_write_text(HMI_SCREEN_NOTICE, HMI_CTRL_NOTICE_VALIDITY,
                   (err_code == 255u) ? "\xB1\xBE\xB4\xCE\xB2\xE2\xC1\xBF\xD2\xD1\xC8\xA1\xCF\xFB" : "\xB1\xBE\xB4\xCE\xCE\xB4\xBB\xF1\xB5\xC3\xD3\xD0\xD0\xA7\xBD\xE1\xB9\xFB");
}

/*
 * 停止测量：发 0xA1 给传感器，状态置 ST_STOPPING 并记录发起时刻；
 * 先弹 notice 页显示“已取消”，再补写 stopping 页文本。
 * 若 1.5s 内没收到 0xA1 应答，app_loop 的超时分支会强制回 IDLE。
 */
static void stop_measure(void)
{
    bp_send_cmd(BP_CMD_STOP, 0, 0);           s_state = ST_STOPPING;
    s_stop_tick = g_tick;                     s_stop_acked = 0;
        notice_show(255u);                            hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_STATUS, "\xD5\xFD\xD4\xDA\xCD\xA3\xD6\xB9");
    hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_DETAIL, "\xD5\xFD\xD4\xDA\xC8\xB7\xC8\xCF\xCD\xA3\xD6\xB9\xD7\xB4\xCC\xAC");
    status_refresh();                         hmi_change_page(HMI_SCREEN_NOTICE);       dbg_str("[BP] STOP requested (0xA1) -> notice page\r\n");
}

static void uart_gpio_init(void){
    GPIO_InitTypeDef g;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB |
                           RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3 | RCC_APB1Periph_UART5, ENABLE);

        GPIO_PinAFConfig(GPIOA, GPIO_PinSource9,  GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
    /* 屏：USART3，PB10(TX)/PB11(RX) */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);
    /* 传感器：UART5，PC12(TX)/PD2(RX) */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource12, GPIO_AF_UART5);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource2,  GPIO_AF_UART5);

    g.GPIO_Mode = GPIO_Mode_AF;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd = GPIO_PuPd_UP;

    g.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;  GPIO_Init(GPIOA, &g);  /* USART1 */
    g.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11; GPIO_Init(GPIOB, &g);  /* USART3 屏 */
    g.GPIO_Pin = GPIO_Pin_12;                GPIO_Init(GPIOC, &g);  /* UART5 TX */
    g.GPIO_Pin = GPIO_Pin_2;                 GPIO_Init(GPIOD, &g);  /* UART5 RX */
}

static void uart_periph_init(void)
{
    USART_InitTypeDef u;
    u.USART_WordLength = USART_WordLength_8b;
    u.USART_StopBits = USART_StopBits_1;
    u.USART_Parity = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

        u.USART_BaudRate = 115200;
    USART_Init(USART1, &u);
    USART_Init(UART5, &u);                       /* 传感器 115200 */

        u.USART_BaudRate = 9600;
    USART_Init(USART3, &u);                       /* 屏 9600 */

    USART_Cmd(USART1, ENABLE);
    USART_Cmd(USART3, ENABLE);
    USART_Cmd(UART5, ENABLE);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);   
   #if RX_USE_DMA_IDLE
        USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);   /* 屏 */
        USART_ITConfig(UART5, USART_IT_IDLE, ENABLE);   /* 传感器 */
#else
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_ITConfig(UART5, USART_IT_RXNE, ENABLE);
#endif
}

static void nvic_init(void)
{
    NVIC_InitTypeDef n;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority = 1;
    n.NVIC_IRQChannelCmd = ENABLE;

    n.NVIC_IRQChannel = USART1_IRQn; NVIC_Init(&n);
    n.NVIC_IRQChannel = USART3_IRQn; n.NVIC_IRQChannelSubPriority = 2; NVIC_Init(&n);  /* 屏 */
    n.NVIC_IRQChannel = UART5_IRQn; n.NVIC_IRQChannelSubPriority = 3; NVIC_Init(&n);  /* 传感器 */
}

#if RX_USE_DMA_IDLE
static void dma_rx_stream_init(DMA_Stream_TypeDef *st, USART_TypeDef *u, uint8_t *mem)
{
    DMA_InitTypeDef d;
    DMA_DeInit(st);
    d.DMA_Channel            = DMA_Channel_4;
    d.DMA_PeripheralBaseAddr = (uint32_t)&(u->DR);
    d.DMA_Memory0BaseAddr    = (uint32_t)mem;
    d.DMA_DIR                = DMA_DIR_PeripheralToMemory;
    d.DMA_BufferSize         = DMA_RX_BUF_SIZE;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    d.DMA_Mode               = DMA_Mode_Normal;
    d.DMA_Priority           = DMA_Priority_High;
    d.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    d.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
    d.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    d.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(st, &d);
    DMA_Cmd(st, ENABLE);
}

static void dma_rx_init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    dma_rx_stream_init(DMA1_Stream1, USART3, s_dma_screen_buf);   /* 屏   RX → Stream1/Ch4 */
    dma_rx_stream_init(DMA1_Stream0, UART5,  s_dma_sensor_buf);   /* 传感器 RX → Stream0/Ch4 */
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
    USART_DMACmd(UART5,  USART_DMAReq_Rx, ENABLE);
}
#endif

/*
 * 应用初始化：上电后由 main() 调用一次。
 * 顺序：GPIO 复用 → NVIC 中断优先级 → SysTick(1ms) → DMA 接收 → UART 外设
 *       → 屏回调注册 → 传感器回调注册 → 软 RTC 基准 → 上电自检(屏诊断+空闲页刷新)。
 */
void app_init(void)
{
    uart_gpio_init();
    nvic_init();
    if (SysTick_Config(SystemCoreClock / 1000)) 
	{ 
		while (1); 
	}  /* 1ms tick */
#if RX_USE_DMA_IDLE
    dma_rx_init();     
#endif
    uart_periph_init();
    hmi_init(hmi_send_cb);
    bp_sensor_init(on_bp_frame);   
    s_rtc_base_tick = g_tick;
    s_rtc_base_sec  = 0;

    dbg_str("BP APP START\r\n");
    bright_apply();              
	screen_diag_on_idle();   
}

/*
 * 主循环（main 里 while(1) 调用）：三大任务——
 *    1) 把屏环形缓冲(s_q2)的字节送 hmi_rx_feed 组帧，按钮事件在 hmi_rx_frame 分发；
 *    2) 把传感器环形缓冲(s_q3)的字节送 bp_sensor_feed 解帧，整帧回调 on_bp_frame 处理；
 *    3) 每 1s 推进软 RTC(clock_tick_1s)，刷新时钟显示与测量计时。
 * 中断只负责“收字节入队”，解析全在主循环做，避免长时间占中断丢字节。
 */
void app_loop(void)
{
    static uint32_t alive_last = 0;
    static uint32_t sec_last = 0;
	
    while (s_q3_tail != s_q3_head) 
	{
        uint8_t b = s_q3[s_q3_tail];
        s_q3_tail = (uint16_t)((s_q3_tail + 1u) % RXQ_SIZE);
        dbg_str("RX3:"); 
		dbg_hex2(b); 
		dbg_str(" ");
        bp_sensor_feed(b);
    }
	
    while (s_q2_tail != s_q2_head) 
	{
        uint8_t b = s_q2[s_q2_tail];
        s_q2_tail = (uint16_t)((s_q2_tail + 1u) % RXQ_SIZE);
        dbg_str("RX2:"); dbg_hex2(b); dbg_str(" ");
        hmi_rx_feed(b);       
	}
	
    if (g_tick - alive_last >= 1000) 
	{
        alive_last = g_tick;
        dbg_str("[");
        dbg_u32(g_tick / 1000);
        dbg_str("s] alive\r\n");
    }

    if (g_tick - sec_last >= 1000) 
	{
        sec_last = g_tick;
        clock_tick_1s();
        clock_broadcast(); 
		if (s_state == ST_MEASURING) 
		{                     
			char b[20];
            uint8_t k = 0, mm, ss;
            s_elapsed_sec = (uint16_t)((g_tick - s_meas_start_tick) / 1000u);
            mm = (uint8_t)(s_elapsed_sec / 60u);
            ss = (uint8_t)(s_elapsed_sec % 60u);
            k = str_app(b, k, "\xD2\xD1\xD3\xC3\xCA\xB1 ");
            k = str_app2(b, k, mm); b[k++] = ':';
            k = str_app2(b, k, ss); b[k] = '\0';
            hmi_write_text(HMI_SCREEN_MEASURING, HMI_CTRL_MEAS_ELAPSED, b);
        }
    }

    if (s_state == ST_STOPPING && !s_stop_acked && (g_tick - s_stop_tick >= 1500u)) 
	{
        s_stop_acked = 1;
        hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_STATUS, "\xD2\xD1\xCD\xA3\xD6\xB9");
        hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_DETAIL, "\xB1\xBE\xB4\xCE\xB2\xE2\xC1\xBF\xD2\xD1\xC8\xA1\xCF\xFB");
        hist_add(0u, 0, 0, 0, 2u,
                 s_year, s_month, s_day,
                 (uint8_t)(s_sec_of_day / 3600u),
                 (uint8_t)((s_sec_of_day / 60u) % 60u));
        s_state = ST_IDLE;
        status_set(ST_TXT_CONTEXT_IDLE, ST_TXT_FOOTER_WAIT, ST_TXT_TIP_WAIT, "\xB1\xBE\xB4\xCE\xB2\xE2\xC1\xBF\xD2\xD1\xC8\xA1\xCF\xFB\xA3\xAC\xBF\xC9\xD6\xD8\xD0\xC2\xB2\xE2\xC1\xBF");
        dbg_str("[BP] STOP timeout (no 0xA1 ack) -> force IDLE\r\n");
    }
}

static const char HEXD[] = "0123456789ABCDEF";
static void dbg_hex2(uint8_t b)
{
    uart_send_byte(DEBUG_UART, (uint8_t)HEXD[b >> 4]);
    uart_send_byte(DEBUG_UART, (uint8_t)HEXD[b & 0x0F]);
}

/*
 * 传感器命令发送：把命令字+数据用 bp_make_cmd 组帧(FF C0 LEN CKSUM CMD DATA)，
 * 通过 UART5 发出。
 */
static void bp_send_cmd(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint8_t buf[32];
    uint8_t n = bp_make_cmd(buf, cmd, data, data_len);
    uart_send_buf(SENSOR_UART, buf, n);
}

/*
 * 开始测量：发 唤醒(0xAA)→启动(0xA0) 给传感器，状态置 ST_MEASURING 并记录起始 tick；
 * 屏切到 measuring 页初始化状态/压力/计时文本，再 status_refresh 刷新所有页公共状态条。
 */
static void bp_start_measure(void)
{
    bp_send_cmd(BP_CMD_WAKE, 0, 0);       
	delay_ms(20);
    bp_send_cmd(BP_CMD_START, 0, 0);  
    s_state = ST_MEASURING;
    s_meas_start_tick = g_tick;
    s_elapsed_sec = 0;
    s_stop_acked = 0;   
    hmi_write_text(HMI_SCREEN_MEASURING, HMI_CTRL_MEAS_STATUS, "\xB2\xE2\xC1\xBF\xD6\xD0");
    hmi_write_text(HMI_SCREEN_MEASURING, HMI_CTRL_MEAS_PRESSURE, "--");
    hmi_write_text(HMI_SCREEN_MEASURING, HMI_CTRL_MEAS_ELAPSED, "\xD2\xD1\xD3\xC3\xCA\xB1 00:00");
    status_refresh();       dbg_str("[BP] measure started\r\n");
}

/*
 * 传感器整帧回调：bp_sensor_feed 解出一帧后调用。按命令字分流——
 *    0xAC 结果 → 存 s_last + result_show 切结果页 + 记历史；
 *    0xA1 停止应答 → 确认已停，记“已取消”历史，回 IDLE；
 *    0xA0 实时气压(~10Hz) → 测量/停止页压力值刷新；
 *    0xAD 错误 → notice 页提示 + 记错误历史。
 */
static void on_bp_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    dbg_str(">>> BP FRAME cmd=");
    dbg_hex2(cmd);
    dbg_str(" len=");
    dbg_u32(len);

    switch (cmd) 
	{
	case BP_CMD_RESULT: 
		{                               
		bp_result_t r;
        bp_parse_result(data, &r);
        dbg_str(" RESULT SYS=");
        dbg_u32(r.systolic);
        dbg_str(" DIA=");
        dbg_u32(r.diastolic);
        dbg_str(" PR=");
        dbg_u32(r.heart_rate);
        dbg_str(" IRR=");
        dbg_u32(r.irregular);
        dbg_str("\r\n");

        s_last.valid = 1;
        s_last.sys = r.systolic; s_last.dia = r.diastolic;
        s_last.pr  = r.heart_rate; s_last.irregular = r.irregular;
        s_last.year = s_year; s_last.mon = s_month; s_last.day = s_day;
        s_last.hour = (uint8_t)(s_sec_of_day / 3600u);
        s_last.min  = (uint8_t)((s_sec_of_day / 60u) % 60u);
        result_show();
			
        hist_add(1u, r.systolic, r.diastolic, r.heart_rate,
                 r.irregular ? 1u : 0u,
                 s_last.year, s_last.mon, s_last.day, s_last.hour, s_last.min);

        hmi_change_page(HMI_SCREEN_RESULT);             
		s_state = ST_IDLE;
        status_refresh();           
		dbg_str("[HMI] result -> result page(2) 110/111/112, hist added\r\n");
        break;
    }

    case BP_CMD_STOP: 
	{                                 
		dbg_str(" STOP ACK\r\n");
        if (s_state == ST_STOPPING && !s_stop_acked) 
		{
            s_stop_acked = 1;
            hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_STATUS, "\xD2\xD1\xCD\xA3\xD6\xB9");
            hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_PRESSURE, "--");
            hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_DETAIL, "\xB1\xBE\xB4\xCE\xB2\xE2\xC1\xBF\xD2\xD1\xC8\xA1\xCF\xFB");
            hist_add(0u, 0, 0, 0, 2u,                                   s_year, s_month, s_day,
                     (uint8_t)(s_sec_of_day / 3600u),
                     (uint8_t)((s_sec_of_day / 60u) % 60u));
            s_state = ST_IDLE;
            notice_show(255u);                               
			status_set(ST_TXT_CONTEXT_IDLE, ST_TXT_FOOTER_WAIT, ST_TXT_TIP_WAIT, "\xB1\xBE\xB4\xCE\xB2\xE2\xC1\xBF\xD2\xD1\xC8\xA1\xCF\xFB\xA3\xAC\xBF\xC9\xD6\xD8\xD0\xC2\xB2\xE2\xC1\xBF");
            dbg_str("[HMI] stopped ack -> notice refreshed, state=IDLE\r\n");
        }
        break;
    }

    case BP_CMD_START: 
	{                                
		bp_pressure_t p;
        bp_parse_pressure(data, &p);
        dbg_str(" PRESSURE=");
        dbg_u32(p.pressure);
        dbg_str(" beat=");
        dbg_u32(p.has_beat);
        dbg_str("\r\n");
        if (s_state == ST_MEASURING) 
		{
            char sp[12];
            uint8_t k = u32_dec(p.pressure, sp);
            sp[k] = '\0';
            hmi_write_text(HMI_SCREEN_MEASURING, HMI_CTRL_MEAS_PRESSURE, sp);
        } else if (s_state == ST_STOPPING) 
		{
            char sp[12];
            uint8_t k = u32_dec(p.pressure, sp);
            sp[k] = '\0';
            hmi_write_text(HMI_SCREEN_STOPPING, HMI_CTRL_STOP_PRESSURE, sp);
        }
        break;
    }

    case BP_CMD_ERROR: 
	{                                
		uint8_t code = len ? data[0] : 0;
        dbg_str(" ERROR code=");
        dbg_u32(code);
        dbg_str("\r\n");
        notice_show(code);
        hist_add(0u, 0, 0, 0, 3u,
                 s_year, s_month, s_day,
                 (uint8_t)(s_sec_of_day / 3600u),
                 (uint8_t)((s_sec_of_day / 60u) % 60u));
        hmi_change_page(HMI_SCREEN_NOTICE);
        s_state = ST_IDLE;
        status_set(ST_TXT_CONTEXT_IDLE, ST_TXT_FOOTER_WAIT, "\xB1\xBE\xB4\xCE\xB2\xE2\xC1\xBF\xD2\xEC\xB3\xA3", "\xC7\xEB\xB0\xB4\xCC\xE1\xCA\xBE\xD6\xD8\xD0\xC2\xB2\xE2\xC1\xBF");
        dbg_str("[HMI] error -> notice page(3)\r\n");
        break;
    }

    default:
        dbg_str(" data=");
        for (uint8_t i = 0; i < len; i++) 
		{ 
			dbg_hex2(data[i]); dbg_str(" "); 
		}
        dbg_str("\r\n");
        break;
    }
}

/*
 * 串口接收“搬运工”：在 USART 中断里被调用，把单字节按来源塞进对应环形缓冲
 *    (s_q2=屏 / s_q3=传感器)。这里绝不做解析——只入队，解析在 app_loop 里做。
 */
void on_uart_rx(USART_TypeDef *u, uint8_t b)
{
    if (u == SCREEN_UART) 
	{
        s_rx2_cnt++;
        {
            uint16_t nh = (uint16_t)((s_q2_head + 1u) % RXQ_SIZE);
            if (nh != s_q2_tail) 
			{ 
				s_q2[s_q2_head] = b; s_q2_head = nh; 
			}         
		}
    } else if (u == SENSOR_UART) 
	{
        s_rx3_cnt++;
        {
            uint16_t nh = (uint16_t)((s_q3_head + 1u) % RXQ_SIZE);
            if (nh != s_q3_tail) 
			{ 
				s_q3[s_q3_head] = b; s_q3_head = nh; 
			}
        }
    } else 
	{
        uart_send_byte(DEBUG_UART, b);
    }
}

#if RX_USE_DMA_IDLE
/*
 * DMA+空闲中断收尾：USART3/UART5 收到一帧(线路空闲)才进一次。
 * 步骤：读 DMA 剩余计数算已收字节数 → 逐字节调 on_uart_rx 入环形缓冲
 *       → 关 DMA / 清标志 / 重置计数 / 重开 DMA（Normal 模式需手动重启）。
 */
void on_uart_rx_idle(USART_TypeDef *u)
{
    DMA_Stream_TypeDef *st;
    uint8_t *buf;
    uint16_t remain, got, i;

    if (u == SCREEN_UART)      
	{ 
		st = DMA1_Stream1; buf = s_dma_screen_buf; 
	}
    else if (u == SENSOR_UART) 
	{ 
		st = DMA1_Stream0; buf = s_dma_sensor_buf; 
	}
    else return;

    remain = DMA_GetCurrDataCounter(st);
    got = (remain > DMA_RX_BUF_SIZE) ? 0 : (uint16_t)(DMA_RX_BUF_SIZE - remain);

    for (i = 0; i < got; i++) on_uart_rx(u, buf[i]);

    DMA_Cmd(st, DISABLE);
    while (DMA_GetCmdStatus(st) != DISABLE) { }
    if (st == DMA1_Stream1) 
	{
        DMA_ClearFlag(st, DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TEIF1 |
                          DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1);
    } else 
	{
        DMA_ClearFlag(st, DMA_FLAG_TCIF0 | DMA_FLAG_HTIF0 | DMA_FLAG_TEIF0 |
                          DMA_FLAG_DMEIF0 | DMA_FLAG_FEIF0);
    }
    DMA_SetCurrDataCounter(st, DMA_RX_BUF_SIZE);
    DMA_Cmd(st, ENABLE);
}
#endif
