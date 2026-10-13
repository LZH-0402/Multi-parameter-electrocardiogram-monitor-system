/////////////////////////////////////////////////////////////
///                                                       ///
///                    【注意事项】                       ///
///    此文件中的所有函数均为回调函数，函数会在特定的     ///
///    时间节点被调用。此文件中不能自行添加、修改函数     ///
///    或者添加、修改函数参数,添加修改的函数和参数均      ///
///    无效。                                             ///
///                                                       ///
/////////////////////////////////////////////////////////////

/*
函数：on_init
功能：系统执行初始化
*/
void on_init()
{
	//局部变量定义，只能在函数的最前面定义。

	///
	///画面【on_init】
	//初始化电压为10、电流为2
	"电压" = 10;
	"电流" = 2;

	///
	///画面【on_variant_upate】
	//初始化速度为30、时间为4
	"速度" = 30;
	"时间" = 4;


	///
	///画面【各种运算方式】
	//初始化A、B
	"A" = 1;
	"B" = 2;
	
	
}

/*
函数：on_systick
功能：定期执行任务(1秒/次)
*/
void on_systick()
{
	//局部变量定义，只能在函数的最前面定义。
	
	///
	///画面【on_systick】
	//每次增加一秒
	++"运行时间";

	///
	///画面【系统变量_只读】
	"临时_通信故障" = sys.com_err;
	"临时_系统上电时间" = sys.tick;
	"临时_年" = sys.year;
	"临时_月" = sys.month;
	"临时_日" = sys.day;
	"临时_星期" = sys.week;
	"临时_时" = sys.hour;
	"临时_分" = sys.minute;
	"临时_秒" = sys.second;
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
	//局部变量定义，只能在函数的最前面定义。
	if(timer_id == 0)
	{	
		///
		///画面【on_timer】
		//计数
		"计数"++;
	}
}

/*
函数：on_variant_upate
功能：串口通信导致变量更新时，执行此函数
*/
void on_variant_upate()
{
	//局部变量定义，只能在函数的最前面定义。

	///
	///画面【on_variant_upate】
	//操作符'@'用于判定某个寄存器是否发生改变
	if(@"速度" || @"时间")           
	{
		//速度或者时间发生变化时，重新计算里程
		"里程" = "速度" * "时间";
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
	//局部变量定义，只能在函数的最前面定义。
	int i = 0;
	///
	///画面【on_timer】
	if(screen_id == 5)
	{
		if(control_id == 1 && value == 1)
		{
			//开启定时器0，周期1000ms，倒计时，重复
			start_timer(0, 1000, 1, 0 );
		}
		if(control_id == 2 && value == 1)
		{
			//停止定时器
			stop_timer(0);
		}
	}	

	///
	///画面【on_control_notify】
	if(screen_id == 7)
	{
		if(control_id == 1)
		{
			//运行按钮
			if(value == 0)
			{	
				//弹起通知
				"通知次数" ++;
			}
			else if(value == 1)
			{
				//按下通知
				"通知次数" ++;
			}
			else if(value == 2)
			{
				//长按通知
				"通知次数" ++;
			}
		}
		else if(control_id == 2)
		{
			//文本控件
			"通知次数" ++;
		}
		else if(control_id == 3)
		{
			//图标控件
			if(value == 0)
			{	
				//弹起通知
				"通知次数" ++;
			}
			else if(value == 1)
			{
				//按下通知
				"通知次数" ++;
			}
		}
		else if(control_id == 4)
		{			
			//选择控件
			"通知次数" ++;
		}
		else if(control_id == 5)
		{			
			//菜单
			"通知次数" ++;
		}
		else if(control_id == 6)
		{			
			//滑块
			"通知次数" ++;
		}

	}	

	///
	///画面【各种运算方式】
	if(screen_id == 10)  
	{	
		//运行按钮，value == 1 为单次按下
		if(control_id == 3 && value == 1)  
		{
			"A加B" = "A" + "B";
			"A减B" = "A" - "B";
			"A乘B" = "A" * "B";
			"A除B" = "A" / "B";
			"A%B" = "A" % "B";

			"A位或B" = "A" | "B";
			"A位与B" = "A" & "B";
			"A异或B" = "A" ^"B";
			"A取反" = ~"A";

			"A逻辑或B" = "A" || "B";
			"A逻辑与B" = "A" && "B";
			"A逻辑非" = !"A";
 		
			"A等B" = "A"=="B";	
			"A不等B" = "A"!="B";	
			"A<=B" = "A"<="B";	
			"A>=B" = "A">="B";	
			"A小于B" = "A"<"B";	
			"A大于B" = "A">"B";	
		}
	}

	///
	///画面【系统变量_读写】
	if(screen_id == 11)   
	{
		//设置波特率（按钮按下时设置）
		if(control_id == 1 && value == 1)
		{
			sys.baudrate = 9600;
		}
		else if(control_id == 2 && value == 1)
		{
			sys.baudrate = 115200;
		}

		//设置蜂鸣器（按钮按下时设置）
		if(control_id == 3 && value == 1)
		{	
			//禁用
			sys.beep_en = 0;
		}
		else if(control_id == 4 && value == 1)
		{
			//使用
			sys.beep_en = 1;
		}

		//滑块调整背光亮度
		if(control_id == 5)
		{
			sys.backlight = value;
		}

		//画面ID
		if(control_id == 6 && value == 1)
		{	
			//按钮按下时返回主页（主页画面ID：0）
			sys.current_screen = 0;
		}
	}

 
	///
	///画面【系统功能函数】
	if(screen_id == 13)
	{
		//设置日期时间为（2018-02-01 12:31:01）
		if(control_id == 1 && value == 1)
		{
			set_date(2018,02,01);
			set_time(12,31,01);
		}

		//设置油量、实际功率、IP地址
		if(control_id == 5 && value == 1)
		{
			//set(variant, value);    
			//    参数variant  类型可以为数值、字符串类型的变量
			//    参数value  类型可以为常量、变量；直接支持数值赋值给字符串变量，字符串赋值给数值变量；
			set("油量", 40);
			set("实际功率", "额定功率");
			set("IP地址", "默认IP");
		}

 		//隐藏控件
		if(control_id == 6 && value == 1)
		{
			//隐藏滑块控件
			hide(13, 8);
		}	

 		//显示控件
		if(control_id == 7 && value == 1)
		{
			//显示滑块控件
			show(13, 8);
		}	
	}	

 	///
	///画面【数组与for循环】
	if(screen_id == 14)
	{
		if(control_id == 11 && value == 1)
		{
			//使用for循环设置数组元素值
			for(i = 0;  i < 10; ++i)
			{
				"费用"[i] = "费用"[i] + 1; 
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
	//局部变量定义，只能在函数的最前面定义。

	///
	///画面【IP配置】
	if(screen_id == 9)
	{
		//画面切换到IP配置页面，重新设置一下IP相关参数
		"IP地址" = "默认IP";		//注意：变量"IP地址"为字符串类型，暂不支持直接对字符串类型变量直接赋值为一个字符串常量；
		                            //需要先建立一个字符串类型的内存变量"默认IP"，默认值设置为相应的字符串常量，然后将该
									//内存变量赋值给变量；以下类似。
		"子网掩码" = "默认掩码";
		"网关" = "默认网关";
		"服务器IP" = "默认服务器" ;
	}	
}

