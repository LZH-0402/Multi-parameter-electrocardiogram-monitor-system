--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:                main.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:                4G例程，TCP链接
**
**--------------------------------------------------------------------------
** Created By:                林绍佳
** Created date:            2020/08/21
** Version:                    V1.0    
** Descriptions:            4G例程，TCP链接
**
**--------------------------------------------------------------------------
** Modified by:
** Modified date:
** Version:
** Description:
**
***************************************************************************--]]

--[[
--使用PC虚拟串口屏调试4G模块时使用
--屏幕需要升级工程 UART3_TO_UART1
--使用虚拟屏与实体屏联调时，设置air_debug_mode = 1
--使用实体屏允许时，设置air_debug_mode = 0
--]]
local air_debug_mode = 0
uart_free_protocol = air_debug_mode      --自由串口协议


--[[
-- 合宙 TCP 测试平台
-- http://tcplab.openluat.com
--]]
local Tcp_server_ip   = '180.97.81.180'
local Tcp_server_port = 55932

--[[
--运营商代码   COPS 命令使用
46000,        "CHINA MOBILE",     "CN"  中国移动TD 
46001,        "CHN-CUGSM",        "CN"  中国联通 
46002,        "CHINA MOBILE",     "CN"  中国移动GSM
46003,        "CHINA TELECOM",    "CN"  中国电信CDMA
--]]
local mobile_MCCMNC       = { ['46000']='中国移动', ['46001']='中国联通', ['46002']='中国移动', ['46003']='中国电信'}    --移动编号，用于显示当前运营商
local setup_mobile_mccmnc = { ['46000']='CMIOT', ['46001']='uninet', ['46002']='CMIOT', ['46003']='emome'}         --移动接入点，用于设置网络接入点
local my_mobile_MCCMNC    = ''

--定时获取天气和信号强度的回调函数的定时器
local timerId_Sig_Weather = 0
local timerId_Tcp_recv    = 1
local timer0_notify_cnt   = 0

--画面ID
local sc_Tcp = 0
local Tcp_state = 0

--[[***************************************************************************
** Function name:  my_print
** Descriptions:   打印虚拟屏调试信息，设置 en_debug = 1
                   注意：实体屏运行时，必须设置 en_debug = 0
***************************************************************************--]]
local en_debug_msg = 0 --0-不显示调试信息;1-虚拟屏; 2-实体屏，串口1输出
function my_print(debug_msg)
    if en_debug_msg == 1
    then
        print(debug_msg)
		
	elseif en_debug_msg == 2
    then
        uart_send_string(debug_msg)
    end
end

--[[*********************************************************************
** Function name  : split
** Descriptions   : 切割字符串
** @ str          : 源字符串
** @ pat          : 分割符号
*********************************************************************--]]

function split(str, pat)                                     --切割字符串

	local t = {}
	local last_end = 0
	local s, e = string.find(str, pat, 1)                    --第一个分割号
	local i = 1
	while s do	

		table.insert(t, string.sub(str, last_end + 1, last_end + s - last_end - 1))
		last_end = e

		s, e = string.find(str, pat, last_end + 1)

		i = i + 1
	end
	if last_end <= #str then
		cap = string.sub(str, last_end + 1)
		table.insert(t, cap)
	end
	return t  

end


--[[***************************************************************************
** Function name:  on_init
** Descriptions:   系统初始化时，执行此回调函数。
                   注意：回调函数的参数和函数名固定不能修改
***************************************************************************--]]
function on_init()
    
    dofile('air724at.lua')                                                      --加载 http.lua 文件
    uart_set_baudrate3(115200)                                                   --设置与4G模块通讯的串口3的波特率为115200
    
    air_set_callback(on_air_send_cb,on_air_resp_callback,on_air_log_cb)     --设置4G库函数的命令发送函数，命令回调函数、调试信息打印函数
    
    air_hw_int()                                                            --4G模块初始化设置
 
   -- start_timer(timerId_Sig_Weather, 1000 , 0, 0)                           --开启自动获取型号强度、时间、天气功能
    
end

--[[***************************************************************************
** Function name:  on_timer
** Descriptions:   定时器超时回调
***************************************************************************--]]
function on_timer(timer_id)
    on_air_timer(timer_id)                                  --4G库函数的定时处理
    
    --自动获取型号强度、时间、天气的定时处理
    if timer_id == timerId_Sig_Weather
    then
        timer0_notify_cnt = timer0_notify_cnt + 1           --定时器计数，timer0_notify_cnt 每秒+1。计时长度30min
        if timer0_notify_cnt%15 == 0
        then
            --每15s调用一次，更新信号值
            at_cops_csq()
            timer0_notify_cnt = 0                           --标志位清空，重新开始30min计时
        end
	elseif timer_id == timerId_Tcp_recv
	then
	--	at_tcp_recv(1, 1, 1)
    end
end
local rool_mode = 0


--[[***************************************************************************
** Function name:  on_air_log_cb
** Descriptions:   4G模块-调试信息输出
***************************************************************************--]]
function on_air_log_cb(msg)
	--数据打印到记录控件
	record_add(sc_Tcp,88,string.sub(msg,1,100))
	
	if rool_mode == 1
	then
		local count_record = record_get_count(sc_Tcp, 88)
		record_setoffset(sc_Tcp, 88, count_record)
		record_select(sc_Tcp, 88, (count_record - 1))	
	end
	
end

--[[***************************************************************************
** Function name:  on_air_send_cb
** Descriptions:   4G模块-发送数据
***************************************************************************--]]
function on_air_send_cb(msg)
	if air_debug_mode==1  
	then
		uart_send_string(msg)                       -- air_debug_mode==1 ，使用虚拟屏调试，通过串口1发送数据
		my_print(msg)
	else
		uart_send_string3(msg)                      -- air_debug_mode==0 ，使用实体屏调试，通过串口3发送数据
	end
end

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
        return              --value为空时退出
    end
  
    if value ~= nil
	then
        if string.find(value,'FROM') ~= nil 
		then
			--+RECV FROM: 60.166.12.210:7500
            --分割成3份，分别为+RECV FROM、60.166.12.210、7500
			local serverInfo = split(value,':')
			set_text(sc_Tcp, 7, serverInfo[2])
			set_text(sc_Tcp, 8, serverInfo[3])
			
		
		elseif  string.find(value,'IPD') ~= nil 
		then
			--+IPD,TCP,10:123456789
           -- 分割成2分，分别为+IPD,TCP,10、123456789

			local serverData = split(value,':')
			set_text(sc_Tcp, 9, serverData[2])
		end
    end
	
	if key == nil
    then 
        return              --value为空时退出
    end
	--***************************************************************************
	--条件：    4G初始化完成
	--功能：    使用使用AT指令获取信号强度和运营商。
    --          使用 http get 请求天气、北京时间。
	--调用函数：at_cops_csq()
    --函数功能：获取信号强度和运营商
    --调用函数：get_wea_and_time()
    --函数功能：请求天气、北京时间
	--error [string "--[[*****************************Copyright(c)..."]:218: bad argument #1 to 'find' (string expected, got nil)
	--***************************************************************************
    if string.find(key,'+SAPBR=1,1') ~= nil and string.find(value,'OK') ~= nil 
    then
		at_cops_csq()
		at_tcp_client_cofig()
    end

    --***************************************************************************
	--条件：    4G模块返回运营商信息
	--功能：    设置显示运营商
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
        set_text( sc_Tcp, 11, mobile_MCCMNC[my_mobile_MCCMNC])
        my_print( mobile_MCCMNC[my_mobile_MCCMNC] )
    end
    
    --***************************************************************************
	--条件：    4G模块返回信号强度信息
	--功能：    设置4G模块的信号等级
	--***************************************************************************
    if string.find(key,'+CSQ') ~= nil and string.find(value,'+CSQ') ~= nil
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
			--set_text( sc_Tcp, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'2G')
			set_value( sc_Tcp, 10, 1)                       --设置信号图标显示第1帧 
        elseif my_csq>=12 and my_csq<=13 
        then
            --set_text( sc_Tcp, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'2G')
			set_value( sc_Tcp, 10, 2)                       --设置信号图标显示第2帧 
        elseif my_csq>=14 and my_csq<=15 
        then
            --set_text( sc_Tcp, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'3G')
			set_value( sc_Tcp, 10, 3)                       --设置信号图标显示第3帧 
        elseif my_csq>=16 
        then
            --set_text( sc_Tcp, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'4G')
			set_value( sc_Tcp, 10, 4)                       --设置信号图标显示第4帧 
        end
    end
end

--[[***************************************************************************
** Function name : at_cops_csq
** Descriptions  : 获取运营商信息、信号强度
** @return       : nil,无返回值
***************************************************************************--]]
function at_cops_csq()
    air_cmd_add('AT+COPS?','OK',1000)
    air_cmd_add('AT+CSQ'  ,'OK',1000)
end
--在on_air_resp_callback()中处理返回的信息
----获取运营商信息 end-----

--[[***************************************************************************
** Function name : at_tcp_client_cofig
** Descriptions  : TCP配置
** @return       : nil,无返回值
***************************************************************************--]]
function at_tcp_client_cofig()
	
	local function my_get_ip_cb(key, value)
		
		if string.find(key,'+CIFSR') ~= nil and value ~= nil 
		then
			Tcp_ip = value
			set_text(sc_Tcp, 1, Tcp_ip)
		end
	end
	
	local function my_ip_state(key, value)
		air_log('my debugMsg key   -> '..key)
		air_log('my debugMsg value -> '..value)
		
		if string.find(key,'+CIPSTATUS') ~= nil and string.find(value,'IP STATUS') ~= nil 
		then
			Tcp_state = 1
			air_log('at_tcp_client_cofig success !!!!!!')
			
			--start_timer(timerId_Tcp_recv, 1000, 1, 0)
		end
	end
	
	air_cmd_add('AT+CREG=1',         'OK', 1000, 3) --设置当前GPRS注册状态
	air_cmd_add('AT+CREG?',          'OK', 1000, 3) --查询当前GPRS注册状态
	
	air_cmd_add('AT+CGREG=1',        'OK', 1000, 3) --设置当前GPRS注册状态
	air_cmd_add('AT+CGREG?',         'OK', 1000, 3) --查询当前GPRS注册状态

	air_cmd_add('AT+CGATT=1',        'OK', 1000, 3) --设置当前GPRS附着状态
	air_cmd_add('AT+CGATT?',         'OK', 1000, 3) --查看当前GPRS附着状态
	
	air_cmd_add('AT+CIPMUX=0',       'OK', 1000, 3) --设置为单、多链接模式

	air_cmd_add('AT+CIPQSEND=1',     'OK', 1000, 3) --设置为快发模式（推荐使用这种模式）
	
	air_cmd_add('AT+CSTT?',          'OK', 1000, 3) --启动任务，查看APN
	air_cmd_add('AT+CSTT=\"CMIOT\"', 'OK', 1000, 3) --启动任务，设置APN
	
	air_cmd_add('AT+CIICR'         , 'OK', 1000, 3)                              --激活移动场景
	air_cmd_add('AT+CIFSR'         , 'OK', 1000, 3, my_get_ip_cb)              --查询分配的IP地址
	air_cmd_add('AT+CIPSTATUS'     , 'STATE: IP STATUS', 1000, 3, my_ip_state) --查询下链接状态
	
end

--[[***************************************************************************
** Function name : at_tcp_client_connect
** Descriptions  : 连接服务器
** @mode         : 连接模式，TCP\UPD
** @server_ip    : 服务器地址
** @port         : 服务器端口
** @return       : nil,无返回值
***************************************************************************--]]
function at_tcp_client_connect(mode, server_ip, port)
	
	air_cmd_add('AT+CIPSTART=\"'..mode..'\",'..'\"'..server_ip..'\",'..port, 'OK', 1000, 3) --连接服务器
	air_cmd_add('AT+CIPSTATUS', 'STATE: CONNECT OK', 1000, 3) --查询当前GPRS注册状态
	
end

--[[***************************************************************************
** Function name : at_tcp_send
** Descriptions  : TCP、发送数据到服务端
** @str          : 发送数据
** @return       : nil,无返回值
***************************************************************************--]]
function at_tcp_send(str)
	
	air_cmd_add('AT+CIPSEND='..string.len(str) , '>', 1000, 3)              --查询当前GPRS注册状态
	air_cmd_add(str, 'DATA ACCEPT', 1000, 3)                                 --查询当前GPRS注册状态
end

--[[***************************************************************************
** Function name : at_tcp_client_connect
** Descriptions  : 设置接收模式
** @head         : 是否显示头部
** @showtp       : 是否显示协议
** @srip         : 是否显示IP、端口
** @return       : nil,无返回值
***************************************************************************--]]
function at_set_tcp_recv_mode(head, showtp,srip)
	
	air_cmd_add('AT+CIPHEAD='..head,      'OK', 1000, 3) --显示头部
	air_cmd_add('AT+CIPSHOWTP='..showtp,  'OK', 1000, 3) --显示协议
	air_cmd_add('AT+CIPSRIP='..srip,      'OK', 1000, 3) --显示IP、端口

end


--[[***************************************************************************
** Function name : on_control_notify
** Descriptions  : 系统回调函数，用户通过触摸修改控件后，执行此回调函数。
**                 点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
** @return       : nil,无返回值
***************************************************************************--]]
function on_control_notify(screen,control,value)
    
    --***************************************************************************
    --位置：    多媒体下载
    --***************************************************************************
    if screen == sc_Tcp 
	then
        if control == 4 and value == 0 and Tcp_state == 1
		then
			Tcp_server_port = get_text(sc_Tcp, 3)
			at_tcp_client_connect('TCP', Tcp_server_ip, Tcp_server_port)
			at_set_tcp_recv_mode(1, 1, 1)
		
		elseif control == 6
		then
			local send_data = get_text(sc_Tcp, 5)
			at_tcp_send(send_data)
			
		elseif control == 66
		then
			rool_mode = value
		end
        
    end
end

--[[***************************************************************************
** Function name : on_uart_recv_data
** Descriptions  : 接收串口1的数据的回调函数。串口1是普通串口。
**                 虚拟屏调试时, air_debug_mode = 1，处理串口1的数据。
** @packet       : 接收的数据包
** @return       : nil,无返回值
***************************************************************************--]]
function on_uart_recv_data(packet)
    --4G模块直接PC串口
    if air_debug_mode == 1 
	then
        on_air_recv_data(packet)
    end
    
end

--[[***************************************************************************
** Function name:  on_uart_recv_data3
** Descriptions:   接收串口1的数据的回调函数。串口3是连接4G模块。
***************************************************************************--]]
function on_uart_recv_data3(packet)
    on_air_recv_data(packet)
end
