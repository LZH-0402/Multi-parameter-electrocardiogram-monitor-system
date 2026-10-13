--下面列出了常用的回调函数
--更多功能请阅读<<物联型LUA脚本API.pdf>>

--初始化函数
function on_init()
	set_value (0,2,25)	
	set_value (0,1,((180<<16)+305))	
end

--用户通过触摸修改控件后，执行此回调函数。
--点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
--function on_control_notify(screen,control,value)
--end

--用户通过触摸修改控件后，执行此回调函数。
--点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
function on_control_notify(screen,control,value)

	local Temperature = 0	

	if screen == 0 and  control == 1 then		
		Temperature = value & 0xFFFF	
		Temperature = (Temperature - 180)/5
		set_value (0,2,Temperature)	
	end
end