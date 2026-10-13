/****************************************************************************************
**-----------------------------------文件信息--------------------------------------------
** 文件名称:   main.c
** 文件说明:   SPO2_Hospital 血氧监护仪主程序
**
**      以下是该工程的 7 个画面：
**
**      0 主界面    1 趋势    2 报警记录    3 设置
**      4 暂停监护  5 报警与探头状态        6 清空报警记录
**
**  分工：
**      hmi_driver.c / cmd_queue.c / hmi_user_uart.c  ->  大彩协议与串口（未改）
**      app_spo2.c  ->  界面刷新、报警引擎、记录、按键处理（新增）
**      meas.c      ->  测量数据源（新增，默认内置演示信号）
**      main.c      ->  只保留初始化、主循环、上行帧分发
**
**  屏幕工程关键参数：波特率 115200（DeviceBaudRate=7）、800x480、
**  控件通知=按下+松开、画面切换通知=开。
--------------------------------------------------------------------------------------*/
#include "hmi_driver.h"
#include "hmi_user_uart.h"
#include "cmd_queue.h"
#include "cmd_process.h"
#include "hw_config.h"
#include "ulitity.h"
#include "platform_config.h"
#include "app_spo2.h"
#include "meas.h"
#include "dbg_log.h"
#include "stdio.h"

volatile uint32 timer_tick_count = 0;        /* 定时器节拍，10ms / 次 */
uint8  cmd_buffer[CMD_MAX_SIZE];             /* 上行指令缓存           */

/*
*  \brief  程序入口
*/
int main(void)
{
    qsize size = 0;

    Set_System();                            /* 系统时钟（F4 由 SystemInit 配好） */
    Interrupts_Config();                     /* 串口屏串口中断                    */
    systicket_init();                        /* 10ms 节拍                         */

    DbgLog_Init();                           /* USART1 调试日志，PA9/PA10 115200  */
    Dbg_Log("[BOOT] SPO2_Hospital, SYSCLK=%luMHz", SYSCLK_FREQ_HZ / 1000000UL);
    Dbg_Log("[BOOT] USART1 log 115200 OK");

    UartInit(HMI_USART_BAUDRATE);            /* 串口屏波特率，见 platform_config.h */
    Dbg_Log("[BOOT] HMI USART2 %lu OK", (unsigned long)HMI_USART_BAUDRATE);
    queue_reset();

    delay_ms(300);                           /* 等串口屏初始化完毕，必须 >=300ms   */

    Meas_Init();                             /* 测量数据源                        */
#if MEAS_USE_AFE4490
    Dbg_Log("[BOOT] meas source: AFE4490(SPI), algo_ver=0x%04X", MEAS_ALGO_VERSION);
#else
    Dbg_Log("[BOOT] meas source: DEMO signal, algo_ver=0x%04X", MEAS_ALGO_VERSION);
#endif
    App_Init();                              /* 界面初值下发 + 进入主界面          */
    Dbg_Log("[BOOT] init done, enter main loop");

    while (1)
    {
        /* 从缓冲区取一条完整的上行指令 */
        size = queue_find_cmd(cmd_buffer, CMD_MAX_SIZE);

        if (size > 0)
        {
            if (cmd_buffer[1] != 0x07)
            {
                ProcessMessage((PCTRL_MSG)cmd_buffer, size);     /* 分发到 NotifyXxx */
            }
            else
            {
                __disable_irq();                                 /* 0x07 = 屏要求软复位 */
                NVIC_SystemReset();
            }
        }

        App_Tick();                          /* 应用节拍：采样/波形/报警/刷新/趋势 */
        DbgLog_Task();                       /* 日志任务：弹样本打印 + 1s 状态行   */

        /* 特别注意
           MCU 不要无节制地向串口屏发数据，否则屏内部缓存会满导致丢数据
           （标准型 8K / 基本型 4.7K）。
           本工程在 115200bps 下的实际占用：
             波形  WAVE_CHUNK=10 点/帧 @100Hz 采样  ->  约 10 帧/s，≈240B/s
             数值  400ms 一次批量帧                 ->  约 150B/次
             其余  仅在画面切换/按键时产生
           合计远低于 115200bps 的约 11.5KB/s，余量充足。
           若把 TREND_SAMPLE_SEC 调小、或把 WAVE_CHUNK 调大，请重新核算。 */
    }
}

/*!
*  \brief  上行帧分发
*  \param  msg  待处理消息
*  \param  size 消息长度
*/
void ProcessMessage(PCTRL_MSG msg, uint16 size)
{
    uint8  cmd_type     = msg->cmd_type;                     /* 指令类型   */
    uint8  ctrl_msg     = msg->ctrl_msg;                     /* 消息类型   */
    uint8  control_type = msg->control_type;                 /* 控件类型   */
    uint16 screen_id    = PTR2U16(&msg->screen_id);          /* 画面 ID    */
    uint16 control_id   = PTR2U16(&msg->control_id);         /* 控件 ID    */
    uint32 value        = PTR2U32(msg->param);               /* 数值       */

    switch (cmd_type)
    {
    case NOTIFY_TOUCH_PRESS:                                 /* 触摸按下 */
    case NOTIFY_TOUCH_RELEASE:                               /* 触摸松开 */
        NotifyTouchXY(cmd_buffer[1], PTR2U16(cmd_buffer + 2), PTR2U16(cmd_buffer + 4));
        break;

    case NOTIFY_WRITE_FLASH_OK:
        NotifyWriteFlash(1);
        break;

    case NOTIFY_WRITE_FLASH_FAILD:
        NotifyWriteFlash(0);
        break;

    case NOTIFY_READ_FLASH_OK:
        NotifyReadFlash(1, cmd_buffer + 2, size - 6);        /* 去掉帧头帧尾 */
        break;

    case NOTIFY_READ_FLASH_FAILD:
        NotifyReadFlash(0, 0, 0);
        break;

    case NOTIFY_READ_RTC:                                    /* 读 RTC 回包 */
        NotifyReadRTC(cmd_buffer[2], cmd_buffer[3], cmd_buffer[4],
                      cmd_buffer[5], cmd_buffer[6], cmd_buffer[7], cmd_buffer[8]);
        break;

    case NOTIFY_CONTROL:
        if (ctrl_msg == MSG_GET_CURRENT_SCREEN)              /* 画面切换通知 */
        {
            NotifyScreen(screen_id);
        }
        else
        {
            switch (control_type)
            {
            case kCtrlButton:                                /* 按钮   */
                NotifyButton(screen_id, control_id, msg->param[1]);
                break;
            case kCtrlText:                                  /* 文本   */
                NotifyText(screen_id, control_id, msg->param);
                break;
            case kCtrlProgress:                              /* 进度条 */
                NotifyProgress(screen_id, control_id, value);
                break;
            case kCtrlSlider:                                /* 滑动条 */
                NotifySlider(screen_id, control_id, value);
                break;
            case kCtrlMeter:                                 /* 仪表   */
                NotifyMeter(screen_id, control_id, value);
                break;
            case kCtrlMenu:                                  /* 菜单   */
                NotifyMenu(screen_id, control_id, msg->param[0], msg->param[1]);
                break;
            case kCtrlSelector:                              /* 选择   */
                NotifySelector(screen_id, control_id, msg->param[0]);
                break;
            case kCtrlRTC:                                   /* 倒计时 */
                NotifyTimer(screen_id, control_id);
                break;
            default:
                break;
            }
        }
        break;

    case NOTIFY_HandShake:                                   /* 屏握手：重新下发一遍初值 */
        NOTIFYHandShake();
        break;

    default:
        break;
    }
}

/*!
*  \brief  屏握手完成通知
*  \details  这是屏启动后第一次"我准备好了"的信号，此时把全部初值再下发一遍，
*            可以避免屏比 MCU 起得慢导致的初值丢失。
*/
void NOTIFYHandShake(void)
{
    App_PushAll();
}
