--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:               http.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:             4G模块LUA函数库。对AT命令进行封装。
**
**--------------------------------------------------------------------------
** Created By:              刘仁武
** Created date:            2020/08/21
** Version:                 V1.0    
** Descriptions:            4G模块LUA函数库。对AT命令进行封装。
**
**--------------------------------------------------------------------------
** Modified by:
** Modified date:
** Version:
** Description:
**
***************************************************************************--]]
    
local at_timer_id = 20              --发送AT的超时定时器

local gprs_ok = 0

--4G模块上电启动IO
--PWRKEY-Pin40-PE9
local IOPWRKEY=0x0409

--4G模块复位IO
--RESET-Pin39-PE10
local IORESET=0x040A

--4G模块启动定时器，拉低 IOPWRKEY 4秒以上
local setup_4G_timerid = 21
local setup_4G_timeout = 4000

--4G模块复位定时器，拉低 IORESET 200毫秒以上
local reset_4G_timerid = 22
local reset_4G_timeout = 200

--4G模块在线判断
local judg_online_4G_timerid = 23
local judg_online_4G_timeout = 2000

--测试--复位后定时调用初始化函数的定时器
local reset_at_timerid = 24
local reset_at_timeout = 1200

--[[
--运营商代码   COPS 命令使用
46000,        "CHINA MOBILE",     "CN"  中国移动TD 
46001,        "CHN-CUGSM",        "CN"  中国联通 
46002,        "CHINA MOBILE",     "CN"  中国移动GSM
46003,        "CHINA TELECOM",    "CN"  中国电信CDMA
--]]
local setup_mobile_mccmnc = { ['46000']='CMIOT', ['46001']='uninet', ['46002']='CMIOT', ['46003']='emome'}

local http_api_name = ''

--存储接受数据的 string 类型
local resp_string = ''          --当前接受的数据
local resp_string_last = ''     --上一次接受的数据

--存储接受数据的 table 类型，在 HTTPHEAD 、 HTTPREAD 时使用
local http_resp_data = nil      --当前接受的数据
local http_resp_data_count = 0  --应接受的数据总长度
local http_resp_data_idx = 0    --当前接受的数据长度

--HTTP模式
local http_head_mode = 1

--回调函数
local send_callback = nil       --发送回调函数
local resp_callback = nil       --所有4G返回的数据的回调函数
local log_callback = nil        --调试信息回调函数

local http_en_debug = 0    --0-不显示调试信息;1-虚拟屏; 2-实体屏，串口1输出

--[[***************************************************************************
** Function name:  my_http_print
** Descriptions:   打印虚拟屏调试信息，设置 http_en_debug = 1
                   注意：实体屏运行时， http_en_debug 不能等于 1
***************************************************************************--]]
function my_http_print(debug_msg)
    if http_en_debug == 1
    then
        print(debug_msg)
    end
end

--[[***************************************************************************
** Function name:  my_http_uart_log
** Descriptions:   打印实体屏调试信息，设置 http_en_debug = 2
                   注意：虚拟屏运行时，http_en_debug 不能等于 2
***************************************************************************--]]
function my_http_uart_log(debug_msg)
    if http_en_debug == 2
    then
        uart_send_string(debug_msg)
    end
end

--[[***************************************************************************
** Function name:  air_log
** Descriptions :  调试信息回调函数
** @msg         :  调试信息
***************************************************************************--]]
function air_log(msg)
    if log_callback~= nil
    then
        log_callback(msg)
    end
end

--[[***************************************************************************
** Function name:  air_set_callback
** Descriptions :  设置回调函数
** @send_cb     :  向4G发送数据的回调函数
** @resp_cb     :  所有4G返回的数据的回调函数
** @log_cb      :  调试信息回调函数
***************************************************************************--]]
function air_set_callback(send_cb,resp_cb,log_cb)
    send_callback = send_cb
    resp_callback = resp_cb
    log_callback = log_cb
end

-----------------对4G模块的命令发送队列 BEGIN--------------------------
--对4G模块的命令发送队列
List = {}

--[[***************************************************************************
** Function name:  List.new
** Descriptions :  初始化命令队列
***************************************************************************--]]
function List.new()
    return {first = 0, last = -1}
end

--[[***************************************************************************
** Function name:  List.pushBack
** Descriptions :  填充队列
** @list        :  被填充的队列
** @value       :  填入的数据
***************************************************************************--]]
function List.pushBack(list, value)
    local last = list.last + 1
    list.last = last
    list[last] = value
end

--[[***************************************************************************
** Function name:  List.popFront
** Descriptions :  提取队列中的第一个信息
** @list        :  被提取的队列
** @return      :  被提取的数据
***************************************************************************--]]
function List.popFront(list)
    
    local first = list.first
    
    if first > list.last then
        return nil
    end
    
    local value = list[first]
    list[first] = nil
    list.first = first + 1
    
    return value
end
----------------对4G模块的命令发送队列 END--------------------------------

local cmd_list = List.new()         --命令队列
local cmd_currunt = nil             --当前发送的命令

--[[***************************************************************************
** Function name:  air_cmd_add
** Descriptions :  命令添加到队列，当前没有已发送的命令时，发送添加的第一条命令
** @sendstr     :  发送的命令
** @ackstr      :  4G模块对命令的返回的数据
** @timeout     :  返回数据的超时时间
** @retry       :  命令超时的重发次数
** @callback    :  数据返回时的回调函数
***************************************************************************--]]
function air_cmd_add(sendstr,ackstr,timeout,retry,callback)

    local cmd = {}

    if timeout==nil
    then timeout = 0 end

    if retry==nil
    then retry = 1 end

    cmd.send = sendstr
    cmd.ack = ackstr
    cmd.timeout = timeout  
    cmd.retry = retry
    cmd.callback = callback

    List.pushBack(cmd_list,cmd)

    --当前没有指令在执行，立即执行一次
    if cmd_currunt==nil then
    cmd_list_run_next()
    end

end

--[[***************************************************************************
** Function name:  cmd_list_timeout
** Descriptions :  命令超时的处理函数
***************************************************************************--]]
function cmd_list_timeout()
    --当前没有命令时退出
    if cmd_currunt==nil 
    then 
        return 
    end

    --重发次数递减
    if cmd_currunt.retry>0 then
        cmd_currunt.retry = cmd_currunt.retry-1
    end
  
    --重发次数大于0时，重发命令
    if cmd_currunt.retry>0 then
        cmd_list_run()                                          --重发
    else
        --重发次数小于0时
        if cmd_currunt.callback~=nil
        then
            cmd_currunt.callback('timeout',cmd_currunt.send)    --向命令回调函数返回数据
        end
        cmd_list_run_next()                                     --不需要重发，直接发下一条
    end
end

--[[***************************************************************************
** Function name:  cmd_list_run
** Descriptions :  发送命令
***************************************************************************--]]
function cmd_list_run()
    --当前没有命令时退出
    if cmd_currunt==nil
    then 
        return 
    end
    
    --如果命令不为空，则发送命令
    if string.len(cmd_currunt.send)>1
    then
        air_log('OUT: '..cmd_currunt.send)
        
        send_callback(cmd_currunt.send)
        send_callback('\r\n')
    end
    
    --启动超时计时器
    stop_timer(at_timer_id)
    start_timer(at_timer_id,cmd_currunt.timeout,0,1) --单次

end

--[[***************************************************************************
** Function name:  cmd_list_run
** Descriptions :  发送队列中的下一条命令
***************************************************************************--]]
function cmd_list_run_next()
    cmd_currunt = List.popFront(cmd_list)
    cmd_list_run()
end

--[[***************************************************************************
** Function name:  cmd_list_check_ack
** Descriptions :  4G返回数据处理
** @resp        ： 4G返回的数据
***************************************************************************--]]
function cmd_list_check_ack(resp)  
    if cmd_currunt==nil
    then
        resp_callback(nil,resp)                                     --所有数据回调函数
        cmd_list_run_next()                                         --没有命令需要应答，那就发下一条
        return
    else
        resp_callback(cmd_currunt.send,resp)                        --所有数据回调函数
    end
    
    --air_log('user_log resp -> '..resp)
    
    --如果又设置对于命令的回调函数，则进入
    if cmd_currunt.callback ~= nil
    then
        cmd_currunt.callback(cmd_currunt.send,resp)
        if string.find(resp,'+COPS')~=nil                       
        then                                                        -- COPS 响应，获取SIM卡运营商
            cmd_currunt.callback('sim_oper',resp)                   
        elseif  string.find(cmd_currunt.send,'+HTTPACTION')~=nil
            and string.find(resp,'+HTTPACTION')~=nil                 
        then                                                        -- HTTPACTION 响应
            local nums = {}      
            for v in string.gmatch(resp,"%d+") 
            do nums[#nums+1] = v end      
            cmd_currunt.callback('status',nums[2])                  -- 返回 HTTPACTION 的响应值

        elseif string.find(resp,'+HTTPREAD')~=nil                   
        then                                                        -- HTTPREAD 返回数据
            http_head_mode = 1
            http_resp_data_count = tonumber(string.match(resp,"%d+"))
            http_resp_data_idx = 0
            http_resp_data = {}
            cmd_currunt.callback('data_len',http_resp_data_count)   -- 返回这次传输的数据长度
        elseif string.find(resp,'+HTTPHEAD')~=nil                   
        then                                                        -- HTTPHEAD 返回数据
            http_head_mode = 2
            http_resp_data_count = tonumber(string.match(resp,"%d+"))
            http_resp_data_idx = 0
            http_resp_data = {}
            cmd_currunt.callback('head_len',http_resp_data_count)   -- 返回数据总长度
        elseif string.find(resp,'OK')~=nil
        then                                                        -- HTTPHEAD 或者 HTTPREAD 的数据接受结束
            if http_resp_data~=nil then
                local data_head = {[1]='data',[2]='head'}          
                cmd_currunt.callback(data_head[http_head_mode],http_resp_data)    --返回数据
                http_resp_data = nil
            end
        end
    end
  
    --收到应答后立即执行下一条
    if string.find(resp,cmd_currunt.ack)~=nil then
        air_log('user_log -> find data')
        cmd_list_run_next()
    end
end

--[[***************************************************************************
** Function name:  cmd_list_clear
** Descriptions :  清除命令队列
***************************************************************************--]]
function cmd_list_clear()
    cmd_list = nil
    cmd_list = {}
    cmd_list = List.new()
    
end

--[[***************************************************************************
** Function name:  on_air_timer
** Descriptions :  4G模块定时处理接口，在 maiin.lua 的 on_timer 中处理
***************************************************************************--]]
function on_air_timer(timer_id)
    if timer_id==at_timer_id then
        cmd_list_timeout()                      -- 超时计时器
    end
    
    if timer_id==setup_4G_timerid then
        --setup_4G:end
        gpio_set_value(IOPWRKEY,0)              --4G启动功能计时器
    end
    
    if timer_id==reset_4G_timerid then
        --reset_4G:end
        gpio_set_value(IORESET,0)               --4G复位功能计时器
    end
    
    if timer_id==judg_online_4G_timerid then
        cmd_list_run_next()                     --因为没有返回OK，跳到下一条没有回调函数的‘AT’命令
        setup_4G()
        air_log('setup_4G')
    end
    
    --复位后，定时1s开始初始化设置
    if timer_id == reset_at_timerid
    then
        cmd_list_clear()                       --清空现有指令
        reset_init_4G()                        --复位4G模块
    end
end

--[[***************************************************************************
** Function name:  table2str
** Descriptions :  把表转换成字符串
***************************************************************************--]]
function table2str(tbl)    
    if tbl==nil 
    then return '' end
    
    local str = ''
    local count = #(tbl)
    for i=0,count do    
      ch = string.char(tbl[i])          
      str = str .. ch
    end
    return str
end

--[[***************************************************************************
** Function name:  setup_4G
** Descriptions :  4G模块上电
***************************************************************************--]]
function setup_4G()
    --setup_4G:start
    gpio_set_value(IOPWRKEY,1)                              --拉高 IOPWRKEY 引脚
    stop_timer(setup_4G_timerid)
    start_timer(setup_4G_timerid,setup_4G_timeout,0,1)
end

--[[***************************************************************************
** Function name:  reset_4G
** Descriptions :  复位4G模块
***************************************************************************--]]
function reset_4G()
    --reset_4G:start
    gpio_set_value(IORESET,1)                               --拉高 IORESET 引脚    
    cmd_list_clear()                                        --清空现有指令
    stop_timer(reset_4G_timerid)
    start_timer(reset_4G_timerid,reset_4G_timeout,0,1)
    stop_timer(reset_at_timerid)
    start_timer(reset_at_timerid,reset_at_timeout,0,1)
end

--[[***************************************************************************
** Function name:  air_hw_int
** Descriptions :  系统初始化回调函数
***************************************************************************--]]
function air_hw_int()
  --4G模块上电
    gpio_set_out(IOPWRKEY)
    gpio_set_out(IORESET)
    
    stop_timer(judg_online_4G_timerid)
    start_timer(judg_online_4G_timerid,judg_online_4G_timeout,0,1)
    
    
    local function on_get_mccmnc_cb(key,value)
        local my_mobile_oper=''
        if key == 'sim_oper' 
        then
            --**********************************************************************
            --value：            +COPS: 0,2,"46000",7
            --要提取的值：       46000
            --对应正则表达式：   '+COPS: %d*,%d*,"(%d*)"' 
            --**********************************************************************
            local regular_e = '+COPS: %d*,%d*,"(%d*)"'                      --正则表达式
            local my_mobile_mccmnc = string.match(value,regular_e)          --赋值给 my_mobile_mccmnc
            my_http_print('my_mobile_mccmnc: '..my_mobile_mccmnc)
            my_mobile_oper = setup_mobile_mccmnc[my_mobile_mccmnc]
            my_http_print('my_mobile_oper: '..my_mobile_oper)
            air_cmd_add('AT+SAPBR=3,1,\"APN\",\"'..my_mobile_oper..'\"','OK',1000)  --初始化设置
            air_cmd_add('AT+SAPBR=1,1','OK',1000)                           --初始化设置
        end
    end
    
    function judg_online_4G_cb(key,value)
        if string.find(key,'AT') ~= nil and string.find(value,'OK') ~= nil
        then
            stop_timer(judg_online_4G_timerid)
            reset_4G()
            air_log('ATrevOK')
        end
    end
    
    
    --初始化4G模块
    air_cmd_add('AT','OK',500,0x7FFFFFFF,judg_online_4G_cb)
    air_cmd_add('AT','OK',500,0x7FFFFFFF)
    air_cmd_add('AT+CGATT?','+CGATT: 1',1000,0x7FFFFFFF)
    air_cmd_add('','OK',1000)                                       --没有命令，只是等待OK返回
    air_cmd_add('ATE0','OK',1000)                                   --关闭回显模式
    air_cmd_add('AT+COPS?','OK',1000,0,on_get_mccmnc_cb)
    air_cmd_add('AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"','OK',1000)
    
end

--[[***************************************************************************
** Function name : reset_init_4G
** Descriptions  : 复位后的初始化函数
** @return       : nil,无返回值
***************************************************************************--]]
function reset_init_4G()
    --初始化4G模块
    
    --[[***************************************************************************
    ** Function name :  on_reset_get_mccmnc_cb
    ** Descriptions  :   复位测试的回调函数。
    **                 处理复位初始化后得到的运营商信息
    **                 用于设置移动接入点
    ** @return       : nil,无返回值
    ***************************************************************************--]]
    function on_reset_get_mccmnc_cb(key,value)
        local my_mobile_oper=''
        if key == 'sim_oper' 
        then
            --**********************************************************************
            --value：            +COPS: 0,2,"46000",7
            --要提取的值：       46000
            --对应正则表达式：   '+COPS: %d*,%d*,"(%d*)"' 
            --**********************************************************************
            local regular_e = '+COPS: %d*,%d*,"(%d*)"'                  --正则表达式
            local my_mobile_mccmnc=string.match(value,regular_e)        --赋值给 my_mobile_mccmnc
            my_mobile_oper = setup_mobile_mccmnc[my_mobile_mccmnc]
            air_cmd_add('AT+SAPBR=3,1,\"APN\",\"'..my_mobile_oper..'\"','OK',1000)   --初始化设置
            air_cmd_add('AT+SAPBR=1,1','OK',1000)                       --初始化设置
        end
    end
    
    air_cmd_add('AT','OK',1000,0x7FFFFFFF)
    air_cmd_add('AT+CGATT?','+CGATT: 1',1000,0x7FFFFFFF)
    air_cmd_add('','OK',1000) --没有命令，只是等待OK返回
    air_cmd_add('ATE0','OK',1000) --关闭回显
    air_cmd_add('AT+COPS?', 'OK', 1000, 0, on_reset_get_mccmnc_cb)
    air_cmd_add('AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"','OK',1000)
    
end

--[[***************************************************************************
** Function name:  air_set_baudrate
** Descriptions :  设置4G模块波特率
** @baudrate    :  波特率   
***************************************************************************--]]
function air_set_baudrate(baudrate)
    air_cmd_add('AT+IPR='..baudrate,'OK',1000,3)
end

--[[***************************************************************************
** Function name  :  air_http_post
** Descriptions   :  HTTP POST操作
** @url           :  网页URL
** @content_type  :  自定义，参数值
** @post_data     :  写数据
** @callback      :  4G数据回调函数
***************************************************************************--]]
function air_http_post(url,content_type,post_data,callback)
    http_resp_data_count = 0
    http_resp_data_idx = 0

    air_cmd_add('AT+HTTPINIT','OK',1000)  
    air_cmd_add('AT+HTTPPARA=\"URL\",\"'..url..'\"','OK',1000)
    air_cmd_add('AT+HTTPPARA=\"USER_DEFINED\",\"Content-Type: '..content_type..'\"','OK',1000)
        
    air_cmd_add('AT+HTTPDATA='..string.len(post_data)..',20000','DOWNLOAD',5000)
    air_cmd_add(post_data,'OK',5000)
    air_cmd_add('AT+HTTPACTION=1','+HTTPACTION',40000,1,callback)
    air_cmd_add('AT+HTTPREAD','OK',5000,1,callback)
    air_cmd_add('AT+HTTPTERM','OK',1000)
end

--HTTP GET接口，可用于下载小文件(<300K)
--[[***************************************************************************
** Function name  :  air_http_get
** Descriptions   :  HTTP GET接口（ 可用于下载小文件(<300K) ),HTTPREAD 失败时会重发一次
** @url           :  网页URL
** @callback      :  4G数据回调函数
** @para1         :  可选的参数
** @para2         :  自定义，可选的参数值
***************************************************************************--]]
function air_http_get(url,callback,para1,para2)  
    
    http_resp_data_count = 0
    http_resp_data_idx = 0

    air_cmd_add('AT+HTTPINIT','OK',1000)
    if para1==nil or para2==nil
    then
        air_cmd_add('AT+HTTPPARA=\"CID\",1','OK',1000)    
    else
        air_cmd_add('AT+HTTPPARA=\"'..para1..'\",\"'..para2..'\"','OK',1000)    
    end
    
    air_cmd_add('AT+HTTPPARA=\"URL\",\"'..url..'\"','OK',1000)
    air_cmd_add('AT+HTTPACTION=0','+HTTPACTION',40000,1,callback)
    air_cmd_add('AT+HTTPREAD','OK',5000,1,callback)
    air_cmd_add('AT+HTTPTERM','OK',1000)
end

--[[***************************************************************************
** Function name  :  air_http_get
** Descriptions   :  HTTP GET接口（ 可用于下载小文件(<300K) )
** @url           :  网页URL
** @callback      :  4G数据回调函数
** @para1         :  可选的参数
** @para2         :  自定义，可选的参数值
***************************************************************************--]]
function air_http_get_lc(url,callback,para1,para2)  
    http_resp_data_count = 0
    http_resp_data_idx = 0

    air_cmd_add('AT+HTTPINIT','OK',1000)
    
    if para1==nil or para2==nil
    then
        air_cmd_add('AT+HTTPPARA=\"CID\",1','OK',1000)    
    else
        air_cmd_add('AT+HTTPPARA=\"'..para1..'\",\"'..para2..'\"','OK',1000)    
    end
    
    air_cmd_add('AT+HTTPPARA=\"URL\",\"'..url..'\"','OK',1000)
    air_cmd_add('AT+HTTPACTION=0','+HTTPACTION',40000,1,callback)
    air_cmd_add('AT+HTTPREAD','OK',5000,0,callback)
    air_cmd_add('AT+HTTPTERM','OK',1000)
end

--[[***************************************************************************
** Function name  :  air_http_head
** Descriptions   :  文件路径，用于获取HTTP头部信息，例如文件大小，ETAG，修改时间...
** @url           :  网页URL
** @callback      :  4G数据回调函数
***************************************************************************--]]
function air_http_head(url,callback)
    http_resp_data_count = 0
    http_resp_data_idx = 0
    --air_cmd_add('AT+CSQ','OK',1000)
    air_cmd_add('AT+HTTPINIT','OK',3000,3)
    air_cmd_add('AT+HTTPPARA=\"CID\",1','OK',1000)    
    air_cmd_add('AT+HTTPPARA=\"URL\",\"'..url..'\"','OK',1000)
    air_cmd_add('AT+HTTPACTION=2','+HTTPACTION: 2,200',40000,1,callback)
    air_cmd_add('AT+HTTPHEAD','OK',5000,1,callback)
    air_cmd_add('AT+HTTPTERM','OK',1000)
end

--[[***************************************************************************
** Function name  :  air_http_download_break
** Descriptions   :  分段下载接口
** @url           :  网页URL，文件路径
** @break_begin   :  开始位置
** @break_end     :  结束位置
** @callback      :  4G数据回调函数
***************************************************************************--]]
function air_http_download_break(url,break_begin,break_end,callback)
    http_resp_data_count = 0
    http_resp_data_idx = 0
    --air_cmd_add('AT+CSQ','OK',1000)
    air_cmd_add('AT+HTTPINIT','OK',3000,3)
    air_cmd_add('AT+HTTPPARA=\"CID\",1','OK',1000)  
    air_cmd_add('AT+HTTPPARA=\"URL\",\"'..url..'\"','OK',1000)
    air_cmd_add('AT+HTTPPARA=\"BREAK\",'..break_begin,'OK',1000)
    air_cmd_add('AT+HTTPPARA=\"BREAKEND\",'..break_end,'OK',1000)
    air_cmd_add('AT+HTTPACTION=0','+HTTPACTION: 0,206',150000,1,callback)
    air_cmd_add('AT+HTTPREAD','OK',5000,1,callback)
    air_cmd_add('AT+HTTPTERM','OK',1000)
end

--[[***************************************************************************
** Function name  :  air_http_download
** Descriptions   :  下载文件接口
** @url           :  网页URL，文件路径
** @break_size    ： 每次下载的块大小
** @callback      :  4G数据回调函数
** @timeout_retry ： 自定义的下载失败时的重发次数，不设置时默认为3次
***************************************************************************--]]
function air_http_download(url,break_size,callback,timeout_retry)
    local status = 0
    local file_len = 0
    local data_len = 0
    local break_begin = 0
    local break_end = 0
    
    local http_retry_num = 3
    local httphead_retry=0
    local httpread_retry=0
    if timeout_retry ~= nil
    then
        http_retry_num = timeout_retry
    end
    httphead_retry = http_retry_num
    httpread_retry = http_retry_num

    --HTTP DOWNLOAD BREAK 响应
    local function on_http_download_cb(key,value)

        if key=='status' then
            status = value
            read_status_check = cmd_httpaction_check_ack(status)
            air_log('httpread_retry: '..httpread_retry)
            
            if read_status_check == 'retry'
            then
                httpread_retry = httpread_retry - 1
                
                if httpread_retry >= 0
                then
                    air_http_download_break(url,break_begin,break_end,on_http_download_cb)
                    cmd_list_run_next()
                    air_log('retry: '..httpread_retry..','..value)
                    callback('dl_file_read_retry','retry: '..httpread_retry..','..value)
                else
                    cmd_list_run_next()
                    air_log('retry: '..httpread_retry..','..value)
                    callback('dl_file_read_retry','retry: '..httpread_retry..','..value)
                end
            end
            callback('status',value)
        elseif key=='timeout' then
            air_cmd_add('ATE0','OK',1000)
            callback(key,value)  
            my_http_uart_log(' on_http_download_cb key==timeout: '..key..'\r\n ')
        elseif key=='data' then
            callback(key,value)      
            my_http_uart_log(' on_http_download_cb key==data: '..key..'\r\n ')
            data_len = #(value)+1
            break_begin = break_begin+data_len
            break_end = break_begin+break_size-1
            if break_end>=file_len then
                break_end = file_len-1
            end
           
            --继续下载下一个分包
            if break_begin<file_len then
                httpread_retry = http_retry_num
                air_http_download_break(url,break_begin,break_end,on_http_download_cb)
            else
            --下载完成
            callback('finish',file_len)
            my_http_uart_log(' on_http_download_cb key==finish: \r\n ')
            end
        end
    end
    
    --HTTP HEAD 响应
    local function on_http_head_cb(key,value)
        
        if key=='status' then
            status = value
            --set_text(2,11,' h status: '..status)
            head_status_check = cmd_httpaction_check_ack(status)
            air_log('httphead_retry: '..httphead_retry)
            if head_status_check == 'retry'
            then
                httphead_retry = httphead_retry - 1
                
                if httphead_retry >= 0
                then
                    air_http_head(url,on_http_head_cb)
                    cmd_list_run_next()
                    callback('dl_file_head_retry','retry: '..httphead_retry..','..value)
                else
                    cmd_list_run_next()
                    callback('dl_file_head_retry','retry: '..httphead_retry..','..value)
                end
            end
            callback(key,value)
            my_http_uart_log('on_http_head_cb key==status: '..key..'\r\n ')
        elseif key=='timeout' then
            air_cmd_add('ATE0','OK',1000)
            callback(key,value)
            my_http_uart_log('on_http_head_cb key==timeout: '..key..'\r\n ')
        elseif key=='head_len' then --HEAD数据
            callback(key,value)
            my_http_uart_log('on_http_head_cb key==head_len: '..key..'\r\n ')
        elseif key=='head' then --HEAD数据      
            callback(key,value)      
            my_http_uart_log('on_http_head_cb key==head: '..key..'\r\n ')
            local strdata = table2str(value)
            strdata = string.lower(strdata)
            local idx = string.find(strdata,'length')
            my_http_uart_log('idx: '..idx..'\r\n ')
            
            if idx~=nil then
                file_len = tonumber(string.match(strdata,"%d+",idx))
                callback('data_len',file_len)
                
                break_begin = 0
                break_end = break_size-1
                my_http_uart_log('on_http_head_cb type(file_len)'..type(file_len)..' \r\n ')
                if break_end>=file_len then
                    break_end = file_len-1
                    my_http_uart_log('on_http_head_cb break_end>=file_len \r\n ')
                end        
                --my_http_print('on_http_head_cb file_len: '..file_len)
                my_http_uart_log('on_http_head_cb file_len: '..file_len..' \r\n ')
                
                --取得长度后，开始分包下载文件
                httpread_retry = http_retry_num
                air_http_download_break(url,break_begin,break_end,on_http_download_cb)
                my_http_uart_log('on_http_head_cb end \r\n ')
            end
        end
    end
    
    --HTTP HEAD 发起下载请求
    air_http_head(url,on_http_head_cb)    
end

--[[***************************************************************************
** Function name  :  cmd_httpaction_check_ack
** Descriptions   :  HTTPACTION 响应
** @status        :  HTTP 状态码，由远端服务器响应
***************************************************************************--]]
function cmd_httpaction_check_ack(status)
    if string.find(status,'408')~=nil       -- HTTPACTION 网络超时
    then
        return 'retry'
    elseif string.find(status,'601')~=nil   -- HTTPACTION 网络错误
    then
        return 'retry'
    elseif string.find(status,'416')~=nil   -- HTTPACTION 所请求的范围无法满足
    then
        return 'retry'
    elseif string.find(status,'603')~=nil   -- HTTPACTION DNS 错误
    then
        return 'retry'
    end
end

------------MQTT功能封装-----------
--[[***************************************************************************
** Function name  :  mqtt_config
** Descriptions   :  设置 MQTT 相关参数
** @status        :  客户身份
** @username      :  登录服务器的用户名
** @password      :  登录服务器的密码
** @will_qos      :  将要发送的信息的服务质量
** @will_retain   :  保留标志
** @will_topic    :  将要发送的消息的话题
** @will_message  :  将要发送的消息内容
***************************************************************************--]]
function mqtt_config(clientid,username,password,will_qos,will_retain,will_topic,will_message)
    local config = '\"'..clientid..'\"'

    if username~=nil
    then
        config = config..',\"'..username..'\",'..'\"'..password..'\"'
    end

    if will_qos~=nil 
    then
        config = config..','..will_qos..','..will_retain..',\"'..will_topic..'\",\"'..will_message..'\"'
    end  

    air_cmd_add('AT+MCONFIG='..config,'OK',1000)
end

--[[***************************************************************************
** Function name  :  mqtt_tcp_start
** Descriptions   :  设置 MQTT 相关参数
** @srvaddr       :  服务器 IP 地址或 DNS 地址
** @port          :  服务器端口
** @ssl           :  设置为 true 时，使用SSL链接
***************************************************************************--]]
function mqtt_tcp_start(srvaddr,port,ssl)
    local cmd = 'AT+MIPSTART='
    if ssl==true then
        cmd = 'AT+SSLMIPSTART='
    end
    cmd = cmd .. '\"'..srvaddr..'\",'..port
    air_cmd_add(cmd,'CONNECT OK',40000,1)
end

--[[***************************************************************************
** Function name  :  mqtt_connect
** Descriptions   :  客户端向服务器请求会话连接
** @clean_session :  会话状态
** @keepalive     :  保活时间
***************************************************************************--]]
function mqtt_connect(clean_session,keepalive)
    air_cmd_add('AT+MCONNECT='..clean_session..','..keepalive,'CONNACK OK',10000,1)
end

--[[***************************************************************************
** Function name  :  mqtt_pub
** Descriptions   :  本命令从客户端到服务器，或从服务器到客户端，传送应用消息
** @topic         :  应用程序消息的主题
** @qos           :  申请消息的服务质量
** @retain        :  保留标志
** @message       :  消息内容
***************************************************************************--]]
function mqtt_pub(topic,qos,retain,message)
    --注:消息中内嵌
    --双引号请用\22 表达
    --控制字符回 车\r(0x0D)请用\0D 表达；
    --控制字符换行\n （0x0A）请用\0A --表达
    --控制字符\（0x5C）请 用\5C 表达  
    message = string.gsub(message,'\"','\\22');
--  message = string.gsub(message,'\r','\\0D');
--  message = string.gsub(message,'\n','\\0A');
--  message = string.gsub(message,'\\','\\5C');

    local cmd = 'AT+MPUB=\"'..topic..'\",'..qos..','..retain..',\"'..message..'\"'
    local ack = {'OK','PUBACK','PUBCOMP'}
    air_cmd_add(cmd,ack[qos+1],10000,1)
end

--[[***************************************************************************
** Function name  :  mqtt_sub
** Descriptions   :  本命令从客户端到服务器，用于一个或多个订阅主题
** @topic         :  应用程序消息的主题
** @qos           :  申请消息的服务质量
***************************************************************************--]]
function mqtt_sub(topic,qos)
    local cmd = 'AT+MSUB=\"'..topic..'\",'..qos
    air_cmd_add(cmd,'SUBACK',10000,1)
end

--[[***************************************************************************
** Function name  :  mqtt_unsub
** Descriptions   :  本命令从客户端到服务器，用于取消订阅主题
** @topic         :  应用程序消息的主题
***************************************************************************--]]
function mqtt_unsub(topic)
    local cmd = 'AT+MUNSUB=\"'..topic..'\"'
    air_cmd_add(cmd,'UNSUBACK',10000,1)
end

--[[***************************************************************************
** Function name  :  mqtt_disconnect
** Descriptions   :  关闭 MQTT 连接
***************************************************************************--]]
function mqtt_disconnect()
    air_cmd_add('AT+MDISCONNECT','OK',10000,1)
end

--[[***************************************************************************
** Function name  :  mqtt_tcp_close
** Descriptions   :  关闭 TCP 连接
***************************************************************************--]]
function mqtt_tcp_close()
    air_cmd_add('AT+MIPCLOSE','OK',10000,1)
end

--[[***************************************************************************
** Function name  :  mqtt_get_status
** Descriptions   :  查询 MQTT 连接状态
***************************************************************************--]]
function mqtt_get_status()
    air_cmd_add('AT+MQTTSTATU','OK',10000,1)
end

---------------MQTT END-----------------------

--[[***************************************************************************
** Function name  :  at_response
** Descriptions   :  4G应答处理
***************************************************************************--]]
function at_response() 
  
    if string.len(resp_string) >= 1 
    then    
        air_log('IN: '..resp_string)
        --应答处理
        cmd_list_check_ack(resp_string)
    end

    resp_string_last = resp_string
    resp_string = ''
end

--[[***************************************************************************
** Function name  :  on_air_recv_data
** Descriptions   :  对4G模块串口数据回调
***************************************************************************--]]
function on_air_recv_data(packet)
    local count = #(packet)

    for i=0,count do    
        --正在接收数据部分
        if http_resp_data_idx<http_resp_data_count 
        then                                                        --当时4G返回的数据时存入 http_resp_data 中
            http_resp_data[http_resp_data_idx] = packet[i]
            http_resp_data_idx = http_resp_data_idx+1
        else
            ch = string.char(packet[i])     
            --AT指令应答      
            if ch ~= '\n' 
            then
                resp_string = resp_string .. ch
                --air_log('user log : '..ch..'resp_string ('..string.len(resp_string)..')-> '..resp_string)
                if resp_string == '>'
                then
                    my_http_print('IN: '..resp_string)
                    my_http_uart_log('IN: '..resp_string..'\r\n')
                    at_response()
                end
            end      
            if ch=='\n' 
            then
                my_http_print('IN: '..resp_string)
                my_http_uart_log('IN: '..resp_string..'\r\n')
                at_response()
            end
        end
    end
end
----------------------------- air724at.lua END ---------------------------------