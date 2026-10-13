--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:                main.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:             4G例程，MQTT连接Aliyun；
**
**--------------------------------------------------------------------------
** Created By:                林绍佳
** Created date:            2020/08/21
** Version:                    V1.0
** Descriptions:            4G例程，MQTT连接Aliyun；
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

--下载完成后显示图片
local surface=0         --图片显示的全局变量，必须写上
local picture_en=0      --绘图使能

--[[
--运营商代码   COPS 命令使用
46000,        "CHINA MOBILE",     "CN"  中国移动TD 
46001,        "CHN-CUGSM",        "CN"  中国联通 
46002,        "CHINA MOBILE",     "CN"  中国移动GSM
46003,        "CHINA TELECOM",    "CN"  中国电信CDMA
--]]
local mobile_MCCMNC = { ['46000']='中国移动', ['46001']='中国联通', ['46002']='中国移动', ['46003']='中国电信'}    --移动编号，用于显示当前运营商
local setup_mobile_mccmnc = { ['46000']='CMIOT', ['46001']='uninet', ['46002']='CMIOT', ['46003']='emome'}         --移动接入点，用于设置网络接入点

--定时获取天气和信号强度的回调函数的定时器
local timerId_Sig_Weather = 0
local timer0_notify_cnt   = 0


--阿里云服务器的登录id和三元组
local clientId     = '862991419835242'                      --无限制
local productKey   = 'a1D2E9vaSuZ'                          --三元组的 productKey
local deviceName   = 'tuOVqg3nrVZnR2oMWwev'                 --三元组的 deviceName
local deviceSecret = '36c51acc5e3a7e410fe55831fd5e899d'     --三元组的 deviceSecret
local mqtt_sub_bit = 'not_connect'                          --Aliyun连接状态,在第二次进入时，不在重新连接

--画面ID
local screen_ac_control = 0


local en_debug_msg = 0 --0-不显示调试信息;1-虚拟屏; 2-实体屏，串口1输出

--[[***************************************************************************
** Function name:  my_print
** Descriptions:   打印虚拟屏调试信息，设置 en_debug_msg = 1
                   注意：实体屏运行时， en_debug_msg 不能等于 1
***************************************************************************--]]
function my_print(debug_msg)
    if en_debug_msg == 1
    then
        print(debug_msg)
    end
end

--[[***************************************************************************
** Function name:  my_uart_log
** Descriptions:   打印实体屏调试信息，设置 en_debug_msg = 2
                   注意：虚拟屏运行时，en_debug_msg 不能等于 2
***************************************************************************--]]
function my_uart_log(debug_msg)
    if en_debug_msg == 2
    then
        uart_send_string(debug_msg)
    end
end


function on_init()
    
    dofile('air724at.lua')                                                    --加载 http.lua 文件
    uart_set_baudrate3(115200)                                              --设置与4G模块通讯的串口3的波特率为115200
    
    air_set_callback(on_air_send_cb,on_air_resp_callback,on_air_log_cb)   --设置4G库函数的命令发送函数，命令回调函数、调试信息打印函数
    
    air_hw_int()                                                            --4G模块初始化设置

    start_timer(timerId_Sig_Weather, 1000 , 0, 0)                           --开启自动获取型号强度、时间、天气功能

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
        my_uart_log('notify_cnt -> '..timer0_notify_cnt..'\r\n')
        if timer0_notify_cnt%15 == 0
        then
            --每15s调用一次，更新信号值
            at_cops_csq()
        end
    end  
end

--[[***************************************************************************
** Function name:  on_air_log_cb
** Descriptions:   4G模块-调试信息输出
***************************************************************************--]]
function on_air_log_cb(msg)
  --数据打印到记录控件
  --record_add(0,1,string.sub(msg,1,100))
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
		my_uart_log('out: '..msg..'\r\n')
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
    
    --***************************************************************************
	--条件：     接收 Aliyun 发布消息
	--功能：     在 cloud_on_property_set(payload) 中对消息进行处理。
	--调用函数： cloud_on_property_set(payload)
	--***************************************************************************
    if string.find(value,'+MSUB') ~= nil and key == nil
    then
        local index = string.find(value,'{')
        
        my_print('index: '..index)
        
        if index ~= nil
        then
            local payload = string.sub(value, index,-1)
            if payload ~= nil 
			then
                cloud_on_property_set(payload)
            end
        end
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
	
	--***************************************************************************
	--条件：    4G初始化完成
	--功能：    使用使用AT指令获取信号强度和运营商。
    --          使用 MQTT 提交三元组，阿里云认证。
	--调用函数：at_cops_csq()
    --函数功能：获取信号强度和运营商
    --调用函数：aliyun_get_iot_token()
    --函数功能：阿里云认证
	--***************************************************************************
    if string.find(key,'+SAPBR=1,1') ~= nil and string.find(value,'OK') ~= nil 
    then
        at_cops_csq()           --获取信号强度
		aliyun_get_iot_token() --阿里云认证
    end
    
    --***************************************************************************
	--条件：    4G模块返回运营商信息
	--功能：    设置串口波特率为 115200 
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
        set_text( screen_ac_control, 13, mobile_MCCMNC[my_mobile_MCCMNC] )
        my_print( mobile_MCCMNC[my_mobile_MCCMNC] )
    end
    
    --***************************************************************************
	--条件：    4G模块返回信号强度信息
	--功能：    设置串口波特率为 115200 
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
            set_value( screen_ac_control, 12, 1)                       --设置信号图标显示第1帧 
        elseif my_csq>=12 and my_csq<=13 
        then
            set_value( screen_ac_control, 12, 2)                       --设置信号图标显示第2帧 
        elseif my_csq>=14 and my_csq<=15 
        then
            set_value( screen_ac_control, 12, 3)                       --设置信号图标显示第3帧 
        elseif my_csq>=16 
        then
            set_value( screen_ac_control, 12, 4)                       --设置信号图标显示第4帧 
        end
        my_print(my_csq)
    end
    
    --***************************************************************************
	--条件：    连接Aliyun，AT+SSLMIPSTART 成功
	--功能：    发送AT+MCONNECT=...
	--***************************************************************************
    if string.find(key, '+SSLMIPSTART') ~= nil and string.find(value, 'CONNECT OK') ~= nil
    then
        my_print('mqtt_connect(1,600)')
        mqtt_connect(1,600) 
        
    --***************************************************************************
	--条件：    连接Aliyun，AT+MCONNECT 成功
	--功能：    订阅主题
	--***************************************************************************
    elseif string.find(key, '+MCONNECT') ~= nil and string.find(value,'CONNACK OK')~=nil
    then
        mqtt_sub('/sys/'..productKey..'/'..deviceName..'/thing/service/property/set',0)
        mqtt_sub_bit = 'sub_OK'
    end
    
end


-----阿里云------
--[[***************************************************************************
** Function name:  on_aliyun_get_iot_token_cb
** Descriptions :  Aliyun认证回调函数
**                 成功时发送命令 AT+MCONFIG 和 AT+SSLMIPSTART
***************************************************************************--]]
function on_aliyun_get_iot_token_cb(key, value)
    my_print('on_aliyun_get_iot_token_cb'..key)
    
    if key=='data' then
        local jsondata = cjson.decode(table2str(value))          --json解码
        if jsondata ~= nil and jsondata['code']==200
        then
            iotId = jsondata['data']['iotId']
            iotToken = jsondata['data']['iotToken']
            
            my_print('iotId'..iotId)
            my_print('iotToken'..iotToken)
            
            mqtt_config(clientId,iotId,iotToken)
            mqtt_tcp_start(productKey .. '.iot-as-mqtt.cn-shanghai.aliyuncs.com', 1883, true)
        end
    end
end

local my_resp_mode = { [0]='制冷', [1]='除湿', [2]='送风', [3]='自动'}
--[[***************************************************************************
** Function name: cloud_on_property_set
** Descriptions : Aliyun发布消息时，此函数处理消息内容
** @payload     : 字符串，aliyun端->屏幕的数据
** Aliyun发布的消息例子：
** MSUB: "/sys/a1D2E9vaSuZ/PlYPGnoYWeUJujy0li2a/_thing/event/notify",139 byte,{"method":"_thing.event.notify","id":"1294676426","params":{"identifier":"awss.BindNotify","value":{"Operation":"Bind"}},"version":"1.0.0"}
**MSUB: "/sys/a1D2E9vaSuZ/PlYPGnoYWeUJujy0li2a/thing/service/property/set",95 byte,{"method":"thing.service.property.set","id":"2032042748","params":{"mode":3},"version":"1.0.0"}
***************************************************************************--]]

function cloud_on_property_set(payload)
    my_print('cloud_on_property_set')
    
    --payload = string.gsub(payload,'\\22','\"');
    
    local data = cjson.decode(payload) --解析JSON数据
    
    if data == nil 
    then 
        return 
    end
    
    if data['params'] == nil 
    then 
        return 
    end
    
    --开关机按钮
    local powerstate = tonumber(data['params']['powerstate'])
    if powerstate ~= nil
    then 
        set_value(screen_ac_control,1,powerstate)
    end
    
    --滤网时间进度条
    local filter_time = tonumber(data['params']['filter_time'])
    if filter_time ~= nil
    then 
        set_value(screen_ac_control,3,filter_time)
    end
    
    --温度文本
    local targetTemperature = tonumber(data['params']['targetTemperature'])
    if targetTemperature ~= nil
    then 
        set_value(screen_ac_control,4,targetTemperature)
    end
    
    --模式选择控件
    local mode = tonumber(data['params']['mode'])
    if mode ~= nil
    then 
        set_text(screen_ac_control,8,my_resp_mode[mode])
    end

end

local params = {}
local property = {}
local post_id = 1               --屏发布的消息次数ID
--[[***************************************************************************
** Function name:  aliyun_pub
** Descriptions :  屏发布消息时，此函数处理消息内容，并发送
** @screen      : aliyun 画面ID
** @control     : 控件ID
** @value       : 控件值
** @return      : nil,无返回值

** 屏发布的消息例子：
**AT+MPUB="/sys/a1D2E9vaSuZ/PlYPGnoYWeUJujy0li2a/thing/event/property/post",0,0,"{\22version\22:\221.0\22,\22params\22:{\22mode\22:0},\22id\22:\2216\22,\22method\22:\22thing.event.property.post\22}"
**AT+MPUB="/sys/a1D2E9vaSuZ/PlYPGnoYWeUJujy0li2a/thing/event/property/post",0,0,"{\22version\22:\221.0\22,\22params\22:{\22mode\22:0,\22targetTemperature\22:27},\22id\22:\2261\22,\22method\22:\22thing.event.property.post\22}"
***************************************************************************--]]
function aliyun_pub(screen, control, value)
   
    my_print('aliyun_pub2')
   
    params   = nil
    property = nil
    params   = {}
    
	property = {}
    property['method'] = 'thing.event.property.post'
    property['id'] = ''..post_id
    property['version'] = '1.0'
    post_id = post_id + 1
    
	if screen == screen_ac_control
    then
        --按下开关按钮
        if control == 1
        then
            local PowerSwitchName = 'powerstate'            --设置 PowerSwitchName 为变量 powerstate
            params[PowerSwitchName] = value                 --在表写入按钮数值
            property['params'] = params
            local jsonStr = cjson.encode(property)
            --发布消息
            mqtt_pub(
            '/sys/'..productKey..'/'..deviceName..'/thing/event/property/post',
            0,
            0,
            jsonStr
            )
        end 
    end 
    if screen == screen_ac_control
    then
        --按下滑块
        if control == 3
        then
            local PowerSwitchName = 'filter_time'         --设置 PowerSwitchName 为变量 filter_time
            params[PowerSwitchName] = value               --在表写入滑块数值
            property['params'] = params     
            local jsonStr = cjson.encode(property)
            --发布消息
            mqtt_pub(
            '/sys/'..productKey..'/'..deviceName..'/thing/event/property/post',
            0,
            0,
            jsonStr
            )
        end 
    end 
    if screen == screen_ac_control
    then
        --按下温度设置按钮，温度减
        if control == 6 and value == 1
        then
            temp_value = tonumber(get_text(screen_ac_control,4))
            temp_value = temp_value-1
            if temp_value<16 then
                temp_value = 16
            end
            set_text(screen_ac_control,4,temp_value)
            
        --按下温度设置按钮，温度加
        elseif control == 7 and value == 1
        then
            temp_value = tonumber(get_text(screen_ac_control,4))
            temp_value = temp_value+1
            if temp_value>32 then
                temp_value = 32
            end
            set_text(screen_ac_control,4,temp_value)
        end
		
        local PowerSwitchName = 'targetTemperature'      --设置 PowerSwitchName 为变量 targetTemperature
        params[PowerSwitchName] = temp_value             --在表写入温度数值
        property['params'] = params     
        local jsonStr = cjson.encode(property)
        --发布消息
        mqtt_pub(
        '/sys/'..productKey..'/'..deviceName..'/thing/event/property/post',
        0,
        0,
        jsonStr
        )
        
    end 
    if screen == screen_ac_control
    then
        if control == 10
        then
            local PowerSwitchName = 'mode'
            params[PowerSwitchName] = value
            property['params'] = params     
            local jsonStr = cjson.encode(property)    
            --my_print('jsonStr: '..jsonStr)    
            --jsonStr: {"id":"1","params":{"powerstate":1},"version":"1.0","method":"thing.event.property.post"}
            mqtt_pub(
            '/sys/'..productKey..'/'..deviceName..'/thing/event/property/post',
            0,
            0,
            jsonStr
            )
        end 
    end 
end

--[[***************************************************************************
** Function name: aliyun_get_iot_token
** Descriptions : 连接Aliyun
** return       : nil,无返回值
***************************************************************************--]]
function aliyun_get_iot_token()
    
	local msg  = 'clientId'..clientId..'deviceName'..deviceName..'productKey'..productKey
    local sign = md5_hmac(msg,deviceSecret)           --计算哈希值
    local http_data = 'productKey='..productKey..'&sign='..sign..'&clientId='..clientId..'&deviceName='..deviceName
    
    --连接Aliyun
    air_http_post(
                'https://iot-auth.cn-shanghai.aliyuncs.com/auth/devicename',
                'application/x-www-form-urlencoded',
                http_data,
                on_aliyun_get_iot_token_cb
                )
    
    --设置二维码
    qrstring = 'https://g.aliplus.com/ilop/static/download/ilopdownload.html?locale=zh-CN&pk='..productKey..'&dn='..deviceName
    set_text( screen_ac_control, 11, qrstring)

end
----阿里云 END-----


--[[***************************************************************************
** Function name : at_cops_csq
** Descriptions  : 获取运营商信息、信号强度
** @return       : nil,无返回值
***************************************************************************--]]
function at_cops_csq()
    air_cmd_add('AT+COPS?','OK',1000)
    air_cmd_add('AT+CSQ','OK',1000)
end
--在on_air_resp_callback()中处理返回的信息
----获取运营商信息 end-----


--[[***************************************************************************
** Function name : on_control_notify
** Descriptions  : 系统回调函数，用户通过触摸修改控件后，执行此回调函数。
**                 点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
** @return       : nil,无返回值
***************************************************************************--]]
function on_control_notify(screen,control,value)
  
    --***************************************************************************
    --位置：    空调控制器
    --***************************************************************************
    if screen == screen_ac_control 
	then
        if control == 1 or 
           control == 3 or
           control == 6 or
           control == 7 or 
           control == 10
        then
            --屏发布消息
            aliyun_pub(screen,control,value)
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
