--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:                main.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:             4G例程 在线实体屏联调
**                         
**
**--------------------------------------------------------------------------
** Created By:              lin qingtian
** Created date:            2020/08/21
** Version:                    V1.0    
** Descriptions:            4G例程 在线实体屏联调
**
***************************************************************************--]]

--[[
--使用PC虚拟串口屏调试4G模块时使用
--屏幕需要升级工程 UART3_TO_UART1
--使用虚拟屏与实体屏联调时，设置air_debug_mode = 1
--使用实体屏联调时，设置air_debug_mode = 0
--]]

local air_debug_mode = 1
uart_free_protocol = air_debug_mode      --自由串口协议

--[[
--运营商代码   COPS 命令使用
46000,        "CHINA MOBILE",     "CN"  中国移动TD 
46001,        "CHN-CUGSM",        "CN"  中国联通 
46002,        "CHINA MOBILE",     "CN"  中国移动GSM
46003,        "CHINA TELECOM",    "CN"  中国电信CDMA
--]]
local mobile_MCCMNC = { ['46000']='中国移动', ['46001']='中国联通', ['46002']='中国移动', ['46003']='中国电信'}    --移动编号
local setup_mobile_mccmnc = { ['46000']='CMIOT', ['46001']='uninet', ['46002']='CMIOT', ['46003']='emome'}         --移动接入点

--定时获取信号强度的回调函数的定时器
local timerId_Sig_Weather = 0
local timer0_notify_cnt = 0

--画面ID
local screen_main       = 0

local en_debug_msg = 1 --0-不显示调试信息;1-虚拟屏; 2-实体屏，串口1输出
--[[***************************************************************************
** Function name:  my_print
** Descriptions:   打印虚拟屏调试信息，设置 en_debug_msg = 1
                   注意：实体屏运行时，必须设置 en_debug_msg = 0
***************************************************************************--]]
function my_print(debug_msg)
    if en_debug_msg == 1
    then
        print(debug_msg)
    elseif en_debug_msg == 2
    then
        uart_send_string(debug_msg)
    end
end

--[[***************************************************************************
** Function name:  on_air_send_cb
** Descriptions:   4G模块-发送数据
***************************************************************************--]]
function on_air_send_cb(msg)
    if air_debug_mode==1  then
        uart_send_string(msg)                       -- air_debug_mode==1 ，使用虚拟屏调试，通过串口1发送数据
        my_print(msg)
    else
        uart_send_string3(msg)                      -- air_debug_mode==0 ，使用实体屏调试，通过串口3发送数据
        my_print('out: '..msg..'\r\n')
    end
end

--[[***************************************************************************
** Function name:  at_cops_csq
** Descriptions:   获取运营商信息、信号强度
***************************************************************************--]]
function at_cops_csq()
    air_cmd_add('AT+COPS?','OK',1000)
    air_cmd_add('AT+CSQ','OK',1000)
    on_air_resp_other_callback = at_cops_csq_cb   ----用户自定义的4G模块-数据回调函数
end
--在on_air_resp_callback()中处理返回的信息
----获取运营商信息 end-----

--[[***************************************************************************
** Function name:  on_air_resp_callback
** Descriptions :  4G模块-数据回调接口
** @key         :  屏幕->4G模块的发送请求
** @value       :  4G模块->返回的数据
***************************************************************************--]]
function on_air_resp_callback(key, value)  
    
	my_print('on_air_resp_callback()')

    if value == nil
    then 
		my_print('nil')
        return              --value为空时退出
    end
    
--***************************************************************************
	--功能：    判断 key
	--          如果 key 为空，则退出函数。
	--          因为 key 为空时，下方 string.find( key , ) 是不正确的使用。
	--          以下key的处理必须不为空，
	--***************************************************************************
    if key == nil 
	then
        return
    end
    my_print('on_air_resp_callback key   ~= nil ='..key)
	my_print('on_air_resp_callback value ~= nil ='..value)
	--***************************************************************************
	--条件：    4G初始化完成
	--功能：    使用使用AT指令获取信号强度和运营商。
	--调用函数：at_cops_csq()
    --函数功能：获取信号强度和运营商
	--***************************************************************************
    if string.find(key,'+SAPBR=1,1') ~= nil and string.find(value,'OK') ~= nil 
    then
        set_text( screen_main, 16, '')
        at_cops_csq()
    end
    
    --***************************************************************************
	--条件：    4G模块返回运营商信息
 
	--***************************************************************************
    if string.find(key,'+COPS')~=nil and string.find(value,'+COPS')~=nil
    then
        --**********************************************************************
        --value：            +COPS: 0,2,"46000",7
        --要提取的值：       46000
        --正则表达式：       '+COPS:.*,.*,"(%d*)"' 
        --**********************************************************************
        local regular_e = '+COPS:.*,.*,"(%d*)"'                     --正则表达式
        local my_mobile_MCCMNC = string.match( value, regular_e )   --获取的值赋给 my_mobile_MCCMNC 
        set_text( screen_main, 2, mobile_MCCMNC[my_mobile_MCCMNC] )
        my_print( mobile_MCCMNC[my_mobile_MCCMNC] )
    end
    
    --***************************************************************************
	--条件：    4G模块返回信号强度信息

	--***************************************************************************
    if string.find(key,'+CSQ')~=nil and string.find(value,'+CSQ')~=nil
    then
        --**********************************************************************
        --value：            +CSQ: 15,99
        --要提取的值：       15
        --正则表达式：       '+CSQ: (.*),.*' 
        --**********************************************************************
        local regular_e = '+CSQ: (.*),.*'                       --正则表达式
        local my_csq = tonumber(string.match(value,regular_e))  --获取的值赋给 my_csq 
        if my_csq<=11 
        then
            set_value( screen_main, 1, 1)                       --设置信号图标显示第1帧 
			set_text( screen_main, 5, "弱")                     --设置文本，显示“弱”
        elseif my_csq>=12 and my_csq<=13 
        then
            set_value( screen_main, 1, 2)                       --设置信号图标显示第2帧 
			set_value( screen_main, 1, 4)                       --设置信号图标显示第4帧 
			set_text( screen_main, 5, "良")                     --设置文本，显示“中”
        elseif my_csq>=14 and my_csq<=15 
        then
            set_value( screen_main, 1, 3)                       --设置信号图标显示第3帧 
			set_text( screen_main, 5, "良")                     --设置文本，显示“中”
        elseif my_csq>=16 
        then
            set_value( screen_main, 1, 4)                       --设置信号图标显示第4帧 
			set_text( screen_main, 5, "强")                     --设置文本，显示“高”
        end
        my_print(my_csq)
    end
        
end

--[[**************************************************************************
** Function name:  on_init
** Descriptions:   系统初始化时，执行此回调函数。
                   注意：回调函数的参数和函数名固定不能修改
***************************************************************************--]]
function on_init()
    
    dofile('air724at.lua')                                                      --加载 http.lua 文件
    uart_set_baudrate3(115200)                                              --设置与4G模块通讯的串口3的波特率为115200
    
    air_set_callback(on_air_send_cb,on_air_resp_callback,on_air_log_cb)     --设置4G库函数的命令发送函数，命令回调函数、调试信息打印函数
    
    air_hw_int()                                                            --4G模块初始化设置
    --air_get_iccid()
    
    start_timer(timerId_Sig_Weather, 1000 , 0, 0)                           --开启自动获取型号强度、时间、天气功能

end
--[[***************************************************************************
** Function name:  on_timer
** Descriptions:   定时器超时回调
***************************************************************************--]]
function on_timer(timer_id)
    on_air_timer(timer_id)                                  --4G库函数的定时处理
    
    --自动获取信号强度
    if timer_id == timerId_Sig_Weather
    then
        timer0_notify_cnt = timer0_notify_cnt + 1           --定时器计数，timer0_notify_cnt 每秒+1。计时长度30min
        my_print('notify_cnt -> '..timer0_notify_cnt..'\r\n')
        if timer0_notify_cnt%15 == 0
        then
            --每15s调用一次，更新信号值
            at_cops_csq()
        elseif timer0_notify_cnt > 1800                     
        then                                                
            timer0_notify_cnt = 0                           --标志位清空，重新开始30min计时
        end
    end

end
--[[***************************************************************************
** Function name:  on_uart_recv_data
** Descriptions:   接收串口1的数据的回调函数。串口1是普通串口。
**                 虚拟屏调试时，air_debug_mode = 1，处理串口1的数据。
***************************************************************************--]]
function on_uart_recv_data(packet)
    --4G模块直接PC串口
    if air_debug_mode==1 then
        on_air_recv_data(packet)
    end
    
end
--[[***************************************************************************
** Function name:  on_uart_recv_data3
** Descriptions:   接收串口3的数据的回调函数。串口3是连接4G模块。
***************************************************************************--]]
function on_uart_recv_data3(packet)
    on_air_recv_data(packet)
end