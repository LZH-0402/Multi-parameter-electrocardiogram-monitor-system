#include "stm32f4xx.h"
#include "sys_wdt.h"

/* g_reset_reason 缺省按上电复位计；WDT_Init() 会据 RCC 复位标志改写 */
uint8_t g_reset_reason = RESET_REASON_POWER;

/* IWDG 超时配置（时序计算见 sys_wdt.h 顶部注释） */
#define WDT_PRESCALER   IWDG_Prescaler_128
#define WDT_RELOAD       625u

/* GBK 转义串（AC5 字面量禁含非 ASCII，用 \xNN 写出；已用 gbk() 校准） */
static const char S_RSN_POWER[] = "\xC9\xCF\xB5\xE7\xB8\xB4\xCE\xBB"; /* 上电复位 */
static const char S_RSN_IWDG[]  = "\xBF\xB4\xC3\xC5\xB9\xB7\xB8\xB4\xCE\xBB"; /* 看门狗复位 */
static const char S_RSN_OTHER[] = "\xC6\xE4\xCB\xFC\xB8\xB4\xCE\xBB";         /* 其它复位 */

void WDT_Init(void)
{
    /* 1) 先读复位源，再清标志——顺序不能反，否则读不到 IWDGRST */
    if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET)
    {
        g_reset_reason = RESET_REASON_IWDG;
    }
    else if ((RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET) ||
             (RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET))
    {
        g_reset_reason = RESET_REASON_POWER;
    }
    else
    {
        g_reset_reason = RESET_REASON_OTHER;
    }
    RCC_ClearFlag();   /* 清所有复位标志（含 IWDGRST），确保下次上电能正确识别 */

    /* 2) 配置并启动 IWDG。IWDG_Enable() 后除复位外无法关闭，且一旦使能即从 reload 开始倒数。
     *    因此本函数必须在所有外设初始化完成、即将进入主循环前调用，避免启动期被误咬。 */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(WDT_PRESCALER);
    IWDG_SetReload(WDT_RELOAD);
    /* 严格按 SPL 标准顺序：写保护 -> 配置 -> 重载 -> 使能。
     * LSI 在 IWDG_Enable() 后由硬件自动开启，因此使能前不能等 PVU/RVU
     *（此时 LSI 未运行，标志不会清，会导致死等）。 */
    IWDG_ReloadCounter();   /* 先喂一次，避免刚使能就差一点超时 */
    IWDG_Enable();          /* 启动独立看门狗（LSI 自动开启，从此开始计时） */
}

void WDT_Feed(void)
{
    IWDG_ReloadCounter();   /* KR=0xAAAA，无需写保护；仅由主循环调用 */
}

const char *WDT_ResetReasonName(void)
{
    switch (g_reset_reason)
    {
        case RESET_REASON_IWDG:  return S_RSN_IWDG;
        case RESET_REASON_OTHER: return S_RSN_OTHER;
        default:                 return S_RSN_POWER;
    }
}
