--下面列出了常用的回调函数
--更多功能请阅读<<LUA脚本API.pdf>>

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
local progress = 0
local meter_value = 0
local meter_flag = 0
local Progress_screenid = 5
local Meter_screenid = 6
local Slider_screendid = 7
local RTC_screenid = 8
local Seltor_screenid = 13
local Historicalcurve_screenid = 16
local Audioscreen_id = 17
local Languagescreen_id = 18
local Bootanimation_id = 39
local CatTime = 4

function on_init()
	--set_button_notify_release()
	start_timer(0,1000,0,0)
	local language_id = get_language()
	if language_id == 0 then
		set_value(Languagescreen_id,1,1)
		set_value(Languagescreen_id,2,0)
	end
	if language_id == 1 then
		set_value(Languagescreen_id,2,1)
		set_value(Languagescreen_id,1,0)
	end
end

function on_timer(timer_id)  
 if timer_id==1
  then
    if meter_value <=0                                                       --小于0时，数值加
  	then
  		meter_flag = 0
    end
    if meter_value >=270                                                     --大于0时，数值减
  	then
  		meter_flag = 1
    end
    if meter_flag  ==0
    then
    set_value(Meter_screenid,1,meter_value)
	set_value(Meter_screenid,5,meter_value)                                               --指针转动
	meter_value = meter_value+2                                              --数值增加
	end
	if meter_flag ==1
	then
	set_value(Meter_screenid,1,meter_value)
	set_value(Meter_screenid,5,meter_value)                                               --指针转动
	meter_value = meter_value-2                                              --数值增加
	end  
  end
 if timer_id == 0 then
		local Curve_value = math.random(1,254)
		set_history_graph_value(Historicalcurve_screenid,1,Curve_value)
 end
 if timer_id == 2 then
	if get_current_screen() == Bootanimation_id then
		CatTime = CatTime - 1
		set_value(Bootanimation_id,3,CatTime)
		if CatTime == 0 then
			change_screen(0)
		end
	end	
 end
end




function on_control_notify(screen,control,value)
	if screen == Meter_screenid then
		if control == 4 and value == 1 then
			start_timer(1,15,0,0)
		end
		if control == 4 and value == 0 then
			stop_timer(1)
		end
	end
	
	if screen == Seltor_screenid then
		if control == 1 then
			if value >= 0 and value <= 6 then
				set_value(screen,3,0)
			end
			if value > 6 and value <= 12 then
				set_value(screen,3,1)
			end
			if value > 12 and value <= 18 then
				set_value(screen,3,2)
			end
			if value > 18 and value <= 21 then
				set_value(screen,3,3)
			end
			if value > 21 and value <= 23 then
				set_value(screen,3,4)
			end
		end
	end



	if screen == RTC_screenid then
		if control == 8 and value == 0 then
			local year,mon,day,hour,min,sec,week = get_date_time()
			if week == 0 then week = "日" end
	 		if week == 1 then week = "一" end
			if week == 2 then week = "二" end
			if week == 3 then week = "三" end
			if week == 4 then week = "四" end
			if week == 5 then week = "五" end
			if week == 6 then week = "六" end
			set_text(screen,1,year)
	 		set_text(screen,2,mon)
			set_text(screen,3,day)
			set_text(screen,4,hour)
			set_text(screen,5,min)
			set_text(screen,6,sec)
			set_text(screen,7,week)	
		end
	end
	if screen == Progress_screenid then
		if control == 3
		then
			if value == 0 or value == 2
			then
				progress = progress - 1
				if progress <= 0
				then
					progress = 0
				end
				set_value(screen,1,progress)
				set_value(screen,2,progress)
			end
		elseif control == 1
		then
			set_value(screen, 2, value)
			progress = value
		elseif control == 4
		then
			if value == 0 or value == 2
			then
				progress = progress + 1
				if progress >= 100
				then
					progress = 100
				end
				set_value(screen,1,progress)
				set_value(screen,2,progress)
			end
		end
	end
	if screen == Slider_screendid then
		if control == 2 then
			set_value(screen,3,value)
			set_backlight(value)
		end
		if control == 5 then
			set_value(screen,6,value)
		end
	end
end
function on_video_notify(msg,v1,v2)
	for i = 3, 5 do
		set_visiable(Bootanimation_id,i,0)
	end
	local anim_id = get_current_screen()
	if anim_id == Bootanimation_id and msg == 0 then
		change_screen(0)
	end
end
function on_audio_callback(state)
	set_value(Audioscreen_id,3,0)
	set_value(Audioscreen_id,4,0)
end


function on_screen_change(screen)
	if screen == Bootanimation_id then
		start_timer(2,1000,0,4)
		set_value(screen,3,CatTime)
	end
end