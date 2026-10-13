#include "uno.h"

measurement_t g_meas;
uint8_t      g_new_frame = 0;

/* ---- 接收诊断（供 USART1 观察链路） ---- */
volatile uint32_t g_uno_rx_bytes = 0;   /* 收到的原始字节总数 */
volatile uint32_t g_uno_frames   = 0;   /* 解析成功的完整帧数 */

/* 最近 32 字节的环形缓冲：即使波特率不对（收到乱码）也能看出"到底有没有在发" */
#define UNO_RING_SZ   32u
static uint8_t  uno_ring[UNO_RING_SZ];
static uint8_t  uno_ring_w = 0;

/* ------------------------------------------------------------------
 * UNO 帧格式（基于 AFE4490_library 的 Example-1 二进制帧，兼容 afe_bridge.py）
 *   偏移  内容
 *   [0]   0x0A          同步1
 *   [1]   0xFA          同步2
 *   [2]   0x0A          数据长度低（DATA_LEN = 10）
 *   [3]   0x00          数据长度高
 *   [4]   0x02          类型 = DATA
 *   [5..8]  IR_data     int32 小端（LED1VAL）
 *   [9..12] RED_data    int32 小端（LED2VAL）
 *   [13]   spo2         uint8（0~100；未算出或探头错误时为 0）
 *   [14]   hr           uint8（bpm；0 表示无效）
 *   [15]   0x00         保留
 *   [16]   0x0B          帧尾
 * 共 17 字节。spo2/hr 仅在 AFE 算法算完后更新，其余时间保持上次值。
 * 同步策略：先匹配 5 字节头（0A FA 0A 00 02），再收 10 字节负载 + 帧尾 00 0B。
 * 即使 IR/RED 数据里出现 0x0A/0xFA 也不会误同步，因为头是固定 5 字节序列，
 * 且末尾再用 00 0B 二次校验。
 * ------------------------------------------------------------------ */
typedef enum {
    S0_SYNC1, S1_SYNC2, S2_LENL, S3_LENH, S4_TYPE,
    S5_PAYLOAD, S6_FOOT1, S7_FOOT2
} parse_state_t;

static parse_state_t st     = S0_SYNC1;
static uint8_t       pl[10];
static uint8_t       pl_i   = 0;
static uint8_t       foot1  = 0;

static int32_t rd_i32_le(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0]) |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

void UNO_ParseByte(uint8_t b)
{
    /* 先记账 + 入环，再做状态机判定：这样即使波特率错、同步不上，
       调试口也能看到"字节在进"以及进的到底是什么。 */
    g_uno_rx_bytes++;
    uno_ring[uno_ring_w] = b;
    uno_ring_w = (uint8_t)((uno_ring_w + 1u) & (UNO_RING_SZ - 1u));

    switch (st) {
    case S0_SYNC1:
        if (b == 0x0A) st = S1_SYNC2;
        break;
    case S1_SYNC2:
        st = (b == 0xFA) ? S2_LENL : S0_SYNC1;
        break;
    case S2_LENL:
        st = (b == 0x0A) ? S3_LENH : S0_SYNC1;
        break;
    case S3_LENH:
        st = (b == 0x00) ? S4_TYPE : S0_SYNC1;
        break;
    case S4_TYPE:
        pl_i = 0;
        st = (b == 0x02) ? S5_PAYLOAD : S0_SYNC1;
        break;
    case S5_PAYLOAD:
        pl[pl_i++] = b;
        if (pl_i >= 10) st = S6_FOOT1;
        break;
    case S6_FOOT1:
        foot1 = b;
        st = S7_FOOT2;
        break;
    case S7_FOOT2:
        if (foot1 == 0x00 && b == 0x0B) {
            g_uno_frames++;
            g_meas.ir_raw        = rd_i32_le(&pl[0]);
            g_meas.red_raw       = rd_i32_le(&pl[4]);
            g_meas.spo2          = (int16_t)pl[8];
            g_meas.pulse_rate    = (int16_t)pl[9];
            g_meas.result_valid  = (pl[8] != 0) ? 1 : 0;   /* spo2==0 视为无效 */
            g_meas.probe_state   = (pl[8] != 0) ? 1 : 0;
            g_meas.signal_quality = (pl[8] != 0) ? 100 : 0; /* 占位，见文档 8.1 */
            g_new_frame = 1;
        }
        st = S0_SYNC1;
        break;
    default:
        st = S0_SYNC1;
        break;
    }
}

/* ============================================================
 * 诊断辅助
 * ============================================================ */
void UNO_Reset(void)
{
    st      = S0_SYNC1;
    pl_i    = 0;
    foot1   = 0;
    g_uno_rx_bytes = 0;
    g_uno_frames   = 0;
}

uint8_t UNO_Snapshot(uint8_t *out, uint8_t n)
{
    uint8_t w, i;

    if (out == 0) return 0;
    if (n > (uint8_t)UNO_RING_SZ) n = (uint8_t)UNO_RING_SZ;

    w = uno_ring_w;   /* 单字节读，读一次即快照，避免半途被中断改掉 */
    for (i = 0; i < n; i++)
    {
        out[i] = uno_ring[(uint8_t)((w + UNO_RING_SZ - n + i) & (UNO_RING_SZ - 1u))];
    }
    return n;
}
