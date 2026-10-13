--下面列出了常用的回调函数
--更多功能请阅读<<物联型LUA脚本API.pdf>>

--初始化函数
--function on_init()
--end

--定时回调函数，系统每隔1秒钟自动调用。
--function on_systick()
--end

--用户通过触摸修改控件后，执行此回调函数。
--点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
--function on_control_notify(screen,control,value)
--end

--当画面切换时，执行此回调函数，screen为目标画面。
--function on_screen_change(screen)
--end



local Decimla = 0.0
local Timevalue = 0

function on_control_notify(screen,control,value)
	--小数数值减少调节
	if screen == 3 and control == 4 and value == 2 then
		Decimla = get_value(3, 6) - 0.1
		if Decimla <= 0 then
			set_value(3, 6, 0)
		else
			set_value(3, 6, Decimla)
		end
	end
	
	--小数数值增加调节	
	if screen == 3 and control == 5 and value == 2 
	then
		Decimla = get_value(3, 6) + 0.1
		if Decimla >= 6 then
			set_value(3, 6, 6)
		else
			set_value(3, 6, Decimla)	
		end
	end

	--时间数值减少调节
	if screen == 3 and control == 7 and value == 1 
	then
		Timevalue = Timevalue - 1
		set_value(3, 9, Timevalue)
		if get_value(3, 9) <= 0 then
			Timevalue = 0
		end
		set_text(3, 9, Timevalue..":00")
	end
	
 --时间数值增加调节	
	if screen == 3 and control == 8 and value == 1 
	then
		Timevalue = Timevalue + 1
		set_value(3, 9, Timevalue)

 		if get_value(3, 9) >= 24 then
			Timevalue = 24	
		end
		set_text(3, 9, Timevalue..":00")
	end	
end

