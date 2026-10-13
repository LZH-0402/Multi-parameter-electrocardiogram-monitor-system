////////////////////////////////////////////////////////////
///                                                      ///
///                    【注意事项】                      ///
///    此文件中的所有函数均为回调函数，函数会在特定的    ///
///    时间节点被调用。此文件中不能自行添加、修改函数    ///
///    或者添加、修改函数参数,添加修改的函数和参数均     ///
///    无效。                                            ///
///                                    2018-03-01        ///
////////////////////////////////////////////////////////////

/*
函数：on_init
功能：系统执行初始化
*/
void on_init()
{
	//串口屏上电后执行的操作
}

/*
函数：on_systick
功能：定期执行任务(1秒/次)
*/
void on_systick()
{
	//串口屏运行时每秒执行一次

}

/*
函数：on_timer
功能：定时器超时通知
参数：timer_id，定时器ID
相关操作：
启动定时器：start_timer(timer_id,timeout,countdown,repeat)
            timer_id-定时器ID（0~9）
			timeout-超时时间，毫秒单位
			countdown-0顺计时，1倒计时，决定sys.timer递增或递减
			repeat-重复次数，0表示无穷
停止定时器：stop_timer(timer_id)
定时器数值：sys.timer0~sys.timer9，毫秒单位
*/
void on_timer(int timer_id)
{
}

/*
函数：on_variant_upate
功能：串口通信导致变量更新时，执行此函数
*/
void on_variant_upate()
{
	//操作符'@'用于判定某个寄存器是否发生改变
	//更新RTC时间
	if(@"更新标记")
	{
		set_date("更新时间"[0],"更新时间"[1],"更新时间"[2]);
		set_time("更新时间"[3],"更新时间"[4],"更新时间"[5]);
	}
	

	//“语言”发生变化时，修改设置系统语言
	if(@"语言")
	{
		sys.lang = "语言";
	}
	
}

/*
函数：on_control_notify
功能：控件值更新通知
参数：screen_id，画面ID
参数：control_id，控件ID
参数：value，新值
*/
void on_control_notify(int screen_id,int control_id,int value)
{
	int i;
	if(screen_id == 19)
	{
		//隐藏显示画面
		if(control_id == 5 && value == 1)
		{	
			//隐藏
			for(i = 7; i <=19; ++i)
			{
				hide(19, i);
			}
		}
		else if(control_id == 6 && value == 1)
		{	
			//显示
			for(i = 7; i <=19; ++i)
			{
				show(19, i);
			}
		}
	}
}

/*
函数：on_screen_change
功能：画面切换通知，当前画面ID发生变化时执行此函数
参数：screen_id，当前画面ID
*/
void on_screen_change(int screen_id)
{
}

