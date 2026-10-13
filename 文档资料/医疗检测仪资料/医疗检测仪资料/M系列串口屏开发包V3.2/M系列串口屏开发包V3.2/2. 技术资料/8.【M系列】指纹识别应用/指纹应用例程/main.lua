--下面列出了常用的回调函数
--更多功能请阅读<<LUA脚本API.pdf>>
uart_free_protocol         = 0


--*****指纹相关*************------------
local screen0_main         = 0
local screen2_reg          = 1
local screen7_cleartips    = 2
local screen9_loginfirst   = 3
local screen10_savetips   = 4

local screen4_userrecord   = 5


local clear_screen         = 0

local timer_id0          = 3
local timer1_password    = 4
local timer2_remaintext  = 5
local timer3_door_id     = 6
local timer4_breaksaved  = 7
local timer5_0breakmain  = 8
local timer6_2breakmain  = 9

local current_time       = 0
local login_count        = 0         
local collect            = 0x02
local recording_id       = -1
local Record_Selected    = 0
local Recorduser_Pages   = 1
local usercurrent_Pages  = 1
local Recordcout_Pages   = 1
local sc5record_Pages    = 1

local door_count         = 3000
local current_screen     = 0

local IO0=0x040A  --PE10
local IO1=0x0409  --PE9
local IO2=0x0405  --PE5

local light_level = 2
local token = ";"
local user_msg_tb        = {}


local cmdcode_table          = {
								[1] = 0x0C,
								[2] =0x0D,
								[3] =0x0E,
								[4] =0X0F,
								[5] =0x1D,
								[6] =0x54,
								[7] =0x58,
								[8] =0x47,
								[9] =0x68,
								[10] =0xAA
								} 

local show_msg_tb        ={
                            [1] ="请把手指放在指纹识别器上",
							[2] ="第一次采集成功，请重新按下手指！",
							[3] ="注册成功",
							[4] ="采集异常，请重新按下手指！",
							[5] ="请重新按下手指！",
							[6] ="请重新把信息填充完整",
							[7] ="请在左侧输入手机后4位"
							}



--[[***************************************************************************
** Function name:  logic_split(str, pat)
** Descriptions :  字符串裁剪
** @ str    :      目标字符串
** @ pat    :      字符（字符串中某个字符作为裁剪标记）
***************************************************************************--]]
function logic_split(str, pat)                             -- 分割字符串
    local t = {}
    local last_end = 0

    local s, e = string.find(str, pat, 1)                    --第一个分割号
    if s == nil then
        return t
    elseif e == nil then
        return t
    end
    local i = 1
    while s do    

        table.insert(t, string.sub(str, last_end + 1, last_end + s - last_end - 1))
        last_end = e
        if string.len(str) > 0 then
            s, e = string.find(str, pat, last_end + 1)
            i = i + 1
        else
            s = 0 
        end
    
    end
    if last_end <= #str then
        cap = string.sub(str, last_end + 1)
        table.insert(t, cap)
    end
    return t  
end

--[[***************************************************************************
** Function name:  Search_record()
** Descriptions :  查存储数据
***************************************************************************--]]
function Search_record()
	local record_counts = record_get_count(screen4_userrecord,29)
	for i = 0 ,  record_counts-1 do
		user_msg_tb[i]  = record_read(screen4_userrecord,29,i)
	end
end


function calculate_pages(screen,control)
	local record_counts = record_get_count(screen,control)
	local rpages        = math.modf(record_counts/10)

	if (record_counts%10) > 0 then
		rpages = rpages + 1 
	end
	if rpages == 0 then
		rpages = 1
	end
	return rpages
end

local init_gpio  = 0


--[[******************************************************************
** Function name : saved_Register
** Descriptions  : 保存注册信息
**                 
******************************************************************--]]

function saved_Register()

	local news_name = "NULL"
	local news_phnumber = get_text(screen2_reg,10)
	local add_msg   = ""
	local tb_count  = record_get_count(screen4_userrecord,29)
	if string.len(news_name) >= 1 and recording_id ~= -1 then
		add_msg =add_msg..recording_id..token..news_name..token..news_phnumber..token
		user_msg_tb[tb_count] = add_msg
		record_add(screen4_userrecord,29,add_msg)
		recording_id = -1 

	elseif recording_id == -1 then
		set_text(screen2_reg,2,show_msg_tb[1])
	elseif string.len(news_name) > 1 and news_name == "请输入名字"  then
		set_text(screen2_reg,2,show_msg_tb[6])
	elseif string.len(news_name) < 1  then
		set_text(screen2_reg,2,show_msg_tb[6])
	end
end

--****指纹相关函数end ----------

--[[*********************************************************************
** Function name:  on_init
** Descriptions :  系统初始化时，执行此回调函数。
*********************************************************************--]]
function on_init()
	dofile('fingerprint_lib.lua')                                           --加载库文件
	uart_set_baudrate3(57600)                                               --设置波特率
    air_set_callback(on_air_send_cb,on_air_resp_callback,on_air_log_cb)     --设置指纹库函数的命令发送函数，命令回调函数、调试信息打印函数

    first_init = 1	
	start_timer(timer_id0,1000,1,10)
	Search_record()

end

--[[*********************************************************************
** Function name:  on_timer
** Descriptions :  定时器超时回到调函数。
** @ timer_id    :  定时器ID
*********************************************************************--]]

local timer_count = 5
function on_timer(timer_id)  

	on_air_timer(timer_id)
	
	if timer_id0 == timer_id then
		lib_Automatic_retrieval(0,100)
	elseif timer1_password == timer_id then
		change_screen(screen0_main)
		air_clearpasswork_falg()
	elseif timer2_remaintext == timer_id then
		set_text(screen0_main,4,"请按下手指！")	
		set_value(screen0_main,5,0)
	elseif timer3_door_id    == timer_id then
		
	elseif timer4_breaksaved == timer_id then
		timer_count = timer_count -1 
		set_text(screen10_savetips,4,timer_count.."秒后自动返回")
		if timer_count == 0 then
			timer_count = 5
			change_screen(screen9_loginfirst)
			set_text(screen10_savetips,4,timer_count.."秒后自动返回")
		end
	elseif timer5_0breakmain == timer_id then

	elseif timer5_2breakmain == timer_id then

	end
	
end


--[[*********************************************************************
** Function name:  on_systick
** Descriptions :  定时回调函数，系统每隔1秒钟自动调用。
*********************************************************************--]]
function on_systick()
    local years,mons,days,hours,mins,sec,weeks = get_date_time()
    if sec <10  then
        sec = '0'..sec
    end
    if hours <10  then
        hours = '0'..hours
    end
    if mins <10  then
        mins = '0'..mins
    end
    current_time = years..'-'..mons..'-'..days..' '..hours..':'..mins..':'..sec
	

end

--[[******************************************************************
** Function name : on_control_notify
** Descriptions  : 系统回调函数，用户通过触摸修改控件后，执行此回调函数。
**                    点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
** @ screen       : 控件触发所在的页面
** @return        : 控件触发的ID
** @return        : 控件值
** @return        : nil,无返回值
******************************************************************--]]

function on_control_notify(screen,control,value)

	if screen0_main ==screen and control == 2 and value == 0 then
		set_text(screen2_reg,2,show_msg_tb[7])		
		set_value(screen2_reg,1,0)
		set_text(screen2_reg,7,"")
		set_text(screen2_reg,10,"")
		set_visiable(screen2_reg,4,0)
		set_value(screen2_reg,3,0)
		stop_timer(timer5_0breakmain)
		stop_timer(timer_id0)
		lib_stop_prograss()
	elseif screen0_main ==screen and control == 1 and value == 0 then
		stop_timer(timer5_0breakmain)
	end


	if  screen2_reg == screen and control >= 12 and control <= 21 then

		local psw =  get_text(screen2_reg,10)
		ph_numer = string.len(psw)
		
		if ph_numer >= 4 then
			set_visiable(screen2_reg,4,1)
			set_value(screen2_reg,3,1)
			set_text(screen2_reg,2,show_msg_tb[1])	
			local tb_count  = record_get_count(screen4_userrecord,29)
			lib_Automatic_registration(collect,tb_count)    --注册指纹
			play_sound("3:/sounds/02.mp3")
		end
		
		ph_numer = ph_numer + 1 		
	elseif  screen2_reg == screen and control == 9 and  value == 1 then

	elseif screen2_reg == screen and control == 8 and  value == 1 then

		if recording_id ~= -1 then
			lib_delete_number(recording_id,1)	
			
		end
		recording_id = -1
	end
	
	if screen7_cleartips  == screen and  control == 3 and value == 0 then
		
		lib_stop_prograss()
		lib_clear_fingerprint()
		record_clear(screen4_userrecord,29)
		start_timer(timer_id0,1000,1,1)

	end

	if screen9_loginfirst == screen and  control == 2 and value == 0  then

		set_text(screen2_reg,2,show_msg_tb[7])		
		set_value(screen2_reg,1,0)
		set_text(screen2_reg,7,"")
		set_text(screen2_reg,10,"")
		set_visiable(screen2_reg,4,0)
		set_value(screen2_reg,3,0)
		stop_timer(timer5_0breakmain)
		stop_timer(timer_id0)
		lib_stop_prograss()

	elseif screen9_loginfirst == screen and  control == 1  and value == 0  then
		stop_timer(timer_id0)
		start_timer(timer_id0,1000,1,1)
		set_text(screen0_main,4,"请按下手指!")
		set_value(screen0_main,5,0)
	elseif screen9_loginfirst == screen and  control == 6  and value == 1  then
		stop_timer(timer5_0breakmain)
		lib_stop_prograss()
	end
	if  screen10_savetips == screen and  control == 3 and value == 0  then
		stop_timer(timer4_breaksaved)
	end
end

--[[******************************************************************
** Function name : on_screen_change
** Descriptions  : 系统回调函数，画面切换后执行此回调函数。
**  
** @ screen       : 触发所在的页面

******************************************************************--]]

function on_screen_change(screen)
	current_screen = screen
	if screen == screen0_main  then

		stop_timer(timer_id0)
		start_timer(timer_id0,200,1,1)
		stop_timer(timer5_0breakmain)
		start_timer(timer5_0breakmain,10000,1,1)

	elseif screen == screen4_userrecord then
		Search_record()
		Recorduser_Pages = calculate_pages(screen4_userrecord,29)
		local record_counts = record_get_count(screen4_userrecord,29)
		set_value(screen4_userrecord,9,record_counts)
		set_value(screen4_userrecord,71,Recorduser_Pages)
		
		usercurrent_Pages = 1
		set_value(screen4_userrecord,69,usercurrent_Pages)
	elseif screen == screen9_loginfirst then
		start_timer(timer_id0,200,1,1)
		stop_timer(timer5_0breakmain)
	end

end

--[[***************************************************************************
** Function name:  on_uart_recv_data3
** Descriptions :  指纹模块-屏幕串口
** @packet         :  屏幕接收指纹模块的发送数据
** @value       :  指纹模块->返回的数据
***************************************************************************--]]
function on_uart_recv_data3(packet)
     on_fingerprint_recv_data(packet)
end

--[[***************************************************************************
** Function name:  on_air_resp_callback
** Descriptions :  指纹模块-数据回调接口
** @key         :  屏幕->指纹模块的发送请求
** @value       :  指纹模块->返回的数据
***************************************************************************--]]
function on_air_resp_callback(key, value)  
   
	local add_vlaue = my_addcheckSum(value,2)
	local cmd_vlaue = (value[(#(value)-1)]<< 8 |value[(#(value))])
	local arr = {}

	
	if key == cmdcode_table[7] and add_vlaue == cmd_vlaue then
		stop_timer(timer_id0)
		if key == cmdcode_table[7] and value[9] == 0x02 then
			
		elseif key == cmdcode_table[7] and value[9] == 0x00 then
			local Fingerprint_ID = (value[10]<< 8 |value[11])
			if user_msg_tb[0] ~= nil then 
				for i=0, #(user_msg_tb) do
					arr = logic_split(user_msg_tb[i],token)
					if  tonumber(arr[1]) ==  Fingerprint_ID then
						lib_stop_prograss()
						set_text(screen0_main,4,"识别成功,手机尾号："..arr[3])
						stop_timer(timer2_remaintext)
						start_timer(timer2_remaintext,3000,1,1)
						local add_msg = arr[1]..token..arr[3]..token..current_time..token
						--record_add(screen5_sign_in_record,8,add_msg)
						set_value(screen0_main,5,1)
						change_screen(screen0_main)
						play_sound("3:/sounds/05.mp3")
						start_timer(timer_id0,1000,1,1)
					end
				end
			end
		elseif key == cmdcode_table[7] and value[9] == 0x09 then
			set_text(screen0_main,4,show_msg_tb[5])
			set_value(screen0_main,5,2)
				change_screen(screen0_main)
			play_sound("3:/sounds/06.mp3")
			stop_timer(timer2_remaintext)
			start_timer(timer2_remaintext,1500,1,1)

		elseif key == cmdcode_table[7] and value[9] == 0x03 then
			set_text(screen0_main,4,show_msg_tb[5])
			set_value(screen0_main,5,2)
			play_sound("3:/sounds/06.mp3")
				change_screen(screen0_main)
			stop_timer(timer2_remaintext)
			start_timer(timer2_remaintext,1500,1,1)
		end	
		start_timer(timer_id0,1000,1,1)
	
	elseif  key == cmdcode_table[7] and add_vlaue ~= cmd_vlaue then
		stop_timer(timer_id0)
		start_timer(timer_id0,1000,1,1)
		change_screen(screen0_main)
	end
	
	--注册指令解析
	if key == cmdcode_table[6] and add_vlaue == cmd_vlaue then
		stop_timer(timer_id0)
		set_text(screen2_reg,7,"")
		if key == cmdcode_table[6] and value[9] == 0x24 then
			set_text(screen2_reg,7,"重复存储，与编号"..(value[10]<<8 |value[11]).."冲突！")
			set_text(screen2_reg,2,show_msg_tb[4])
			local tb_count  = record_get_count(screen4_userrecord,29)
			lib_Automatic_registration(collect,tb_count) 
		elseif key == cmdcode_table[6] and value[9] == 0x56 then
			set_text(screen2_reg,2,show_msg_tb[2])
			set_value(screen2_reg,1,1)
			play_sound("3:/sounds/03.mp3")
		elseif key == cmdcode_table[6] and value[9] == 0x57 then
			set_value(screen2_reg,1,2)
		elseif key == cmdcode_table[6] and value[9] == 0x00 then	
			recording_id = record_get_count(screen4_userrecord,29)
			play_sound("3:/sounds/04.mp3")
			start_timer(timer4_breaksaved,1000,1,5)
			set_value(screen2_reg,1,2)
			set_value(screen2_reg,3,2)
			refresh_screen()
			saved_Register()
			change_screen(screen10_savetips)
		elseif key == cmdcode_table[6] and value[9] == 0x02 then
			set_text(screen2_reg,2,show_msg_tb[4])
			set_value(screen2_reg,1,0)
			local tb_count  = record_get_count(screen4_userrecord,29)
			lib_Automatic_registration(collect,tb_count) 
		end	
	elseif  key == cmdcode_table[6] and add_vlaue ~= cmd_vlaue then
			
	end
end

--[[***************************************************************************
** Function name:  on_air_log_cb
** Descriptions:   指纹模块-调试信息输出
***************************************************************************--]]
function on_air_log_cb(msg)
  
end

--[[***************************************************************************
** Function name:  on_air_send_cb
** Descriptions:   指纹模块-发送数据
***************************************************************************--]]
function on_air_send_cb(msg)

	if type(msg) == 'string'
	then
		uart_send_string(msg)                       -- air_debug_mode==1 ，使用虚拟屏调试，通过串口1发送数据
		my_print(msg)	
	elseif type(msg) == 'table'
	then
		uart_send_data3(msg)
	end
   
    
end
