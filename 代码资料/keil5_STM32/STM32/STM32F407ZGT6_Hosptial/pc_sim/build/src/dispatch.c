#include "cmd_process.h"
extern uint8 cmd_buffer[];
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
