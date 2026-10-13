--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:                main.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:                4G例程，功能包括：4G模块初始化；获取天气、时间；获取运营商、信号强度；
**                          HTTP下载大文件并保存到屏的内存中；MQTT连接Aliyun；
**
**--------------------------------------------------------------------------
** Created By:                林绍佳
** Created date:            2020/08/21
** Version:                    V1.0    
** Descriptions:            4G例程，功能包括：4G模块初始化；获取天气、时间；获取运营商、信号强度；
**                          HTTP下载大文件并保存到屏的内存中；MQTT连接Aliyun；
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

local air_debu545151515g_mode = 0
local air_debusdsddsddg_mode = 0

--下载完成后显示图片
local surface=0         --图片显示的全局变量，必须写上
local picture_en=0      --绘图使能

--[[
--运营商代码   
用于设置网络接入点
COPS 命令使用
46000 中国移动 （GSM）
46001 中国联通 （GSM）
46002 中国移动 （TD-S）
46003 中国电信（CDMA）
46004 空（似乎是专门用来做测试的）
46005 中国电信 （CDMA）
46006 中国联通 （WCDMA）
46007 中国移动 （TD-S）
46008
46009 中国联通
46010 
46011 中国电信 （FDD-LTE） 这个是电信的4G网络测试信号频道
46020 中国移动 铁路专用网络,高铁动车的移动GSM_R网,调度用的
--]]
local mobile_MCCMNC = { ['46000']='中国移动', 
                        ['46001']='中国联通', 
                        ['46002']='中国移动', 
                        ['46003']='中国电信', 
                        ['46005']='中国电信', 
                        ['46006']='中国联通', 
                        ['46007']='中国移动', 
                        ['46009']='中国联通', 
                        ['46011']='中国电信', 
                        ['46020']='中国移动' , 
                    }    --移动编号，用于显示当前运营商
local my_setup_mobile_mccmnc = {   ['46000']='CMIOT', 
                                ['46001']='uninet', 
                                ['46002']='CMIOT', 
                                ['46003']='ctnet', 
                                ['46005']='ctnet', 
                                ['46006']='uninet', 
                                ['46007']='CMIOT', 
                                ['46009']='uninet', 
                                ['46011']='ctnet', 
                                ['46020']='CMIOT' 
                            }         --移动接入点，用于设置网络接入点

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
local screen_main = 0
local screen_ac_control = 1
local screen_mmedia_dl = 2
local screen_updata = 3
local screen_4gtest = 5

local dl_reset_flag=0             --http下载文件错误处理，重启

--测试计时器
local sys_timer_en=0

--生产测试的复位功能标志位
local test_reset_bit = ''
local reset_4G_READY = 0

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

--[[***************************************************************************
** Function name:  init_set_enable
** Descriptions :  设置主界面的按钮触摸使能
** @enable      :  0-禁止触摸；1-开启触摸；
***************************************************************************--]]
function init_4g_set_enable(enable)
    set_enable( screen_main, 123, enable)
    set_enable( screen_main, 124, enable)
    set_enable( screen_main, 125, enable)
    set_enable( screen_main, 126, enable)
end

--[[***************************************************************************
** Function name:  on_init
** Descriptions:   系统初始化时，执行此回调函数。
                   注意：回调函数的参数和函数名固定不能修改
***************************************************************************--]]
function on_init()
    
    dofile('air724at.lua')                                                    --加载 http.lua 文件
    uart_set_baudrate3(115200)                                              --设置与4G模块通讯的串口3的波特率为115200
    init_4g_set_enable(0)                                                 --禁止
    
    air_set_callback(on_air_send_cb,on_air_resp_callback,on_air_log_cb)     --设置4G库函数的命令发送函数，命令回调函数、调试信息打印函数
    
    air_hw_int()                                                            --4G模块初始化设置
    air_get_iccid()
    
    
    
    set_visiable(screen_mmedia_dl,6,0)                                      --设置进度条不可见
    set_visiable(screen_mmedia_dl,7,0)                                      --设置进度文本不可见
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
            if get_value(screen_main,10) == 0
            then
                get_wea_and_time()                          --如果天气图标没有更新，则此处在调用一次
            end                                             
        elseif timer0_notify_cnt > 1800                     
        then                                                
            get_wea_and_time()                              --每30min调用一次，更新天气、时间、信号
            timer0_notify_cnt = 0                           --标志位清空，重新开始30min计时
        end
    end
    
end

--[[***************************************************************************
** Function name:  on_air_log_cb
** Descriptions:   4G模块-调试信息输出
***************************************************************************--]]
function on_air_log_cb(msg)
  --数据打印到记录控件
  --record_add(3,2,string.sub(msg,1,100))
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
    --          使用 http get 请求天气、北京时间。
    --调用函数：at_cops_csq()
    --函数功能：获取信号强度和运营商
    --调用函数：get_wea_and_time()
    --函数功能：请求天气、北京时间
    --***************************************************************************
    if string.find(key,'+SAPBR=1,1') ~= nil and string.find(value,'OK') ~= nil 
    then
        set_text( screen_main, 16, '')                      --主界面的初始化显示文本清空
        set_text( screen_mmedia_dl, 5, '')                  --画面‘多媒体下载’的提示清空
        init_4g_set_enable(1)                               --主界面按钮可以触摸
        at_cops_csq()                                       --获取信号强度
        get_wea_and_time()                                  --获取天气、时间
        start_timer(timerId_Sig_Weather, 1000 , 0, 0)                           --开启自动获取型号强度、时间、天气功能
    end
    
    --***************************************************************************
    --条件：    4G模块返回卡号
    --功能：    获取卡号并显示
    --***************************************************************************
    if string.find(value,'+ICCID') ~= nil 
    then
        --**********************************************************************
        --value：            +ICCID: 89860117831003134201
        --要提取的值：       89860117831003134201
        --正则表达式：       '+ICCID: (%d*)' 
        --**********************************************************************
        local regular_e = '+ICCID: (%d*)'                       --正则表达式
        local my_iccid = string.match( value, regular_e)        --获取的值赋给 my_iccid 
        set_text( screen_main, 3, 'SIM卡号ICCID：'..my_iccid)
    end
    
    --***************************************************************************
    --条件：    设置4G模块波特率成功
    --功能：    设置串口波特率为 921600 
    --***************************************************************************
    if string.find(key,'+IPR=921600') ~= nil and string.find(value,'OK') ~= nil 
    then
        uart_set_baudrate3(921600)
    end
    
    --***************************************************************************
    --条件：    设置4G模块波特率成功
    --功能：    设置串口波特率为 115200 
    --***************************************************************************
    if string.find(key,'+IPR=115200') ~= nil and string.find(value,'OK') ~= nil 
    then
        uart_set_baudrate3(115200)
    end
    
    --***************************************************************************
    --条件：    4G模块返回运营商信息
    --功能：    设置串口波特率为 115200 
    --***************************************************************************
    if string.find(value,'+COPS')~=nil
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
    --功能：    设置串口波特率为 115200 
    --***************************************************************************
    if string.find(value,'+CSQ')~=nil
    then
        --set_text(2,12,value)
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
        elseif my_csq>=12 and my_csq<=13 
        then
            set_value( screen_main, 1, 2)                       --设置信号图标显示第2帧 
        elseif my_csq>=14 and my_csq<=15 
        then
            set_value( screen_main, 1, 3)                       --设置信号图标显示第3帧 
        elseif my_csq>=16 
        then
            set_value( screen_main, 1, 4)                       --设置信号图标显示第4帧 
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
    
    --***************************************************************************
    --条件               ：    获取经纬度成功
    --功能               ：    订阅主题
    --@local_longitude   ：    经度
    --@local_latitude    ：    纬度
    --***************************************************************************
    if string.find(value,'+CIPGSMLOC') ~= nil
    then
        --**********************************************************************
        --value：            +CIPGSMLOC: 0,31.241045,121.472313,18/11/08,15:37:30
        --要提取的值：       31.241045    -  纬度
        --                   121.472313   -  经度
        --正则表达式：       '+CIPGSMLOC: %d+,([0-9.]*),([0-9.]*),' 
        --**********************************************************************
        local local_latitude=''
        local local_longitude=''
        local regular_e = '+CIPGSMLOC: %d+,([0-9.]*),([0-9.]*),'            --正则表达式
        local local_latitude,local_longitude = string.match(value,regular_e)--获取的值赋给 local_latitude,local_longitude 
        
        --***************************************************************************
        --条件：    提取经纬度成功
        --功能：    通过 baidu API 获取当前经纬度的地点
        --***************************************************************************
        if local_latitude ~= nil and local_longitude ~= nil
        then
            my_print('local_latitude : '..local_latitude)
            my_print('local_longitude : '..local_longitude)
            baiduAPI_get_local(local_latitude,local_longitude)     --获取经纬度所表示的地点名称
        end
    end
    
    --***************************************************************************
    --任务：    测试
    --条件：    获取信号成功
    --功能：    显示信号值
    --***************************************************************************
    if string.find(value,'+CSQ')~=nil
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
            set_text( screen_4gtest, 2, '弱')                   --设置信号‘弱’
            set_value( screen_4gtest, 3, my_csq)                --显示信号值
        elseif my_csq>=12 and my_csq<=13 
        then
            set_text( screen_4gtest, 2, '良')                   --设置信号‘良’
            set_value( screen_4gtest, 3, my_csq)                --显示信号值
        elseif my_csq>=14 and my_csq<=15 
        then
            set_text( screen_4gtest, 2, '良')                   --设置信号‘良’
            set_value( screen_4gtest, 3, my_csq)                --显示信号值
        elseif my_csq>=16 
        then
            set_text( screen_4gtest, 2, '强')                   --设置信号‘强’
            set_value( screen_4gtest, 3, my_csq)                --显示信号值
        end
    end
    
    --***************************************************************************
    --任务：    测试
    --条件：    HTTP GET成功，进入复位测试阶段
    --功能：    检查复位是否成功，并在成功后重新初始化
    --***************************************************************************
    if test_reset_bit == 'reset_4G' and reset_4G_READY==0
    then
        if string.find(key,'+SAPBR=1,1') ~= nil and string.find(value,'OK') ~= nil 
        then
            reset_4G_READY = 1                                    --此程序段只会运行一次
            set_text( screen_4gtest, 5, '4G模块复位成功！！！')   --显示复位成功
            set_text( screen_mmedia_dl, 5, '')                    --画面‘多媒体下载’的提示清空
            set_fore_color( screen_4gtest, 5, 0x34CC )            --设置文本为绿色
            set_enable( screen_4gtest, 1, 1)                      --设置按钮可以触摸
            set_value( screen_4gtest, 1, 0)                       --设置按钮弹起
            start_timer( timerId_Sig_Weather, 5*1000 , 0, 0)      --重新开启自动获取天气和信号强度功能
        else
            set_text( screen_4gtest, 5, '4G模块正在复位')            --显示复位中
            set_fore_color( screen_4gtest, 5, 0xF800 )            --设置文本为红色
        end
    end
    
    --***************************************************************************
    --任务：    测试
    --条件：    HTTP GET成功，进入复位测试阶段
    --功能：    检查复位是否成功，并在成功后重新初始化
    --***************************************************************************
    if dl_reset_flag == 1
    then
        if string.find(key,'+SAPBR=1,1') ~= nil and string.find(value,'OK') ~= nil 
        then
            set_value(screen_mmedia_dl , 1, 0)
            set_value(screen_mmedia_dl , 2, 0)
            set_value(screen_mmedia_dl , 3, 0)
            set_enable(screen_mmedia_dl, 1, 1)
            set_enable(screen_mmedia_dl, 2, 1)
            set_enable(screen_mmedia_dl, 3, 1)
            set_value( screen_mmedia_dl, 6, 0)                                      --初始化进度条数值
            set_text( screen_mmedia_dl, 7, '下载进度：0%')                          --初始化进度文本数值
            set_text( screen_mmedia_dl , 8, '')
            set_visiable( screen_mmedia_dl, 6, 0)                                   --设置进度条不可见
            set_visiable( screen_mmedia_dl, 7, 0)                                   --设置进度文本不可见
            set_visiable( screen_mmedia_dl, 8, 0)                                   --设置文件大小文本不可见
        end
    end
end

-----HTTP下载回调 start----
--下载文件的全局变量------
local down_file          = nil  --虚拟盘调试使用，保存数据到C盘
local file_len           = 0    --文件总大小

--分段写入：数据写入文件的全局变量----------
local data_len          = 0       --数据总长度
local WriteOnceSize     = 2048    --单次写数据的长度
local write_begin       = 0       --单次写数据的开始位
local write_end         = 0       --单次写数据的结束位
local write_break_value = {}      --单次写的数据缓存
local open_state        = 0       --文件句柄

local FA_OPEN_EXISTING = 0x00
local FA_READ          = 0x01     --读
local FA_WRITE         = 0x02     --写
local FA_CREATE_NEW    = 0x04     --创建新文件
local FA_CREATE_ALWAYS = 0x08     --覆盖创建新文件
local FA_OPEN_ALWAYS   = 0x10     --打开
local add_write        = FA_WRITE|FA_READ           --写模式
local over_write       = FA_CREATE_ALWAYS|FA_WRITE  --创建文件

--数据写入文件的全局变量 end------

local sd_dir                  = ''    -- SD卡路径，调试时写入SD卡
local down_mode               = 0     -- 下载模式，0-下载图片、1-视频、2-音频
local httpDown_dl_process     = {'正在下载图片...','正在下载视频...', '正在下载音乐...'}
local httpDown_dl_process_end = {'图片名称：car.jpg','视频名称：car.mp4', '音乐名称：mydream.mp3'}
local HttpDownSave_FileTb     = {'3:/car.jpg','3:/car.mp4', '3:/mydream.mp3'}
local HttpDownFile_URITb      = {'http://video.gz-dc.com/logo_480320_165kV2.jpg',
                                 'http://video.gz-dc.com/video2_62M9s.mp4',
                                 'http://video.gz-dc.com/music503k32s.mp3'}

--[[***************************************************************************
** Function name: my_getdataLen_Type
** Descriptions : 获取待写入数据的数据类型和长度
** @data        : 代写入的数据
** @return      : datalen,数据大小; data_type,数据类型
***************************************************************************--]]
function my_getdataLen_Type(data)
    my_uart_log('---------- my_getdataLen_Type ----------'..' \r\n')
    local datalen = -1
    local data_type = type(data)        --获取数据类型
    
    --计算数据长度
    --字符串类型
    if data_type == 'string'
    then
        datalen = string.len(data)      --如果数据类型时string，计算字符串的长度
        
    --表/数组类型
    elseif data_type == 'table'
    then
        datalen = #(data) + 1           --如果数据类型时table，计算表的长度
    end
    
    my_uart_log('Lua_debug data Type and Len: '..data_type..' / '..datalen..' \r\n')
    return datalen,data_type
end


--[[***************************************************************************
** Function name : my_write_filedata
** Descriptions  : 数据分段写入文件
** @file         : 文件路径
** @data         : 待写入的数据
** @open_mode    : 打开文件的方式
** @return       : nil,无返回值
***************************************************************************--]]
function my_write_filedata(file, data, open_mode)
    my_uart_log('---------- my_write_filedata ----------'..' \r\n')
    my_uart_log('Lua_debug: file -> '..file..' / data -> '..type(data)..' / open_mode -> '..open_mode..' \r\n')
    
    local count     = 0     --每次写的字节数
    local write_cnt = 0     --‘data’数据一共需要写的次数
    local seek_ops  = 0     --写位置
    local all_byte  = 0     --获取当前文件大小,仅在追加文件末尾写入执行
    
    --获取待写入数据的长度和数据类型
    local wrire_len, data_type = my_getdataLen_Type(data)
    
    local write_byte_Tb = {}

    --open_state == true 表示文件已打开
    if open_state == true
    then
        --获取当前文件大小,仅在追加文件末尾写入执行
        if open_mode == add_write
        then
            all_byte = file_size()
        end
        
        --数据长度大于0
        if wrire_len > 0
        then
            --计算'@data'要写多少次
            write_cnt =  math.modf(wrire_len / WriteOnceSize)
            -- wrire_len 不是 WriteOnceSize 的整数倍时，wrire_len = wrire_len + 1
            if wrire_len % WriteOnceSize > 0
            then
                write_cnt = write_cnt + 1
            end
            my_uart_log('Lua_debug: need write allcnt -> '..write_cnt..' \r\n')
            
            
            for i = 1, write_cnt
            do
                --复位写字节数组
                write_byte_Tb = {}
                
                --计算写的位置
                seek_ops = (i - 1) * WriteOnceSize +all_byte
                
                --显示当前写入（下载）进度
                http_download_process( seek_ops )
                
                --文件偏移失败
                file_seek(seek_ops)
                if offst_result == false 
                then
                    break
                end
                my_uart_log('Lua_debug: cur seek_ops  -> '..seek_ops..' \r\n')
                
                --计算本次写的个数
                count = WriteOnceSize
                if i == write_cnt
                then
                    if wrire_len % WriteOnceSize > 0
                    then
                        --最后一次写入时，count为剩余数据的长度
                        count = wrire_len % WriteOnceSize
                    end
                end
                my_uart_log('Lua_debug: cur write  -> '..i..'th / wrire count '..count..' \r\n')
                
                --填充写入flash的字节数组
                for j = 0, count - 1
                do
                    if data_type == 'string'
                    then
                        --字符串类型，将每个字符转换为字节数组
                        write_byte_Tb[j - 1] = tonumber(string.byte(data, ((i - 1) * WriteOnceSize + j), ((i - 1) * WriteOnceSize + j)))
                        
                    elseif data_type == 'table'
                    then
                        --数组类型，字节赋值
                        write_byte_Tb[j] = data[((i - 1) * WriteOnceSize + j)]
                    end
                end
                
                flush_flash()                                       --写优化
                local IswriteOK = file_write(write_byte_Tb)         --写数据到文件中
                if IswriteOK == false
                then
                    --写入文件失败，当前数据再写一次
                    i = i - 1
                end    
            end
        end
    else
        --open_state == fail 表示打开失败
        
    end
end

--[[***************************************************************************
** Function name: http_download_process
** Descriptions : 计算当前写入（下载）进度
** @cur_process : 当前下载进度
** @return      : nil, 无返回值
***************************************************************************--]]
function http_download_process( cur_process )
   
    local allsize =  string.format('%0.2f', (file_len    / 1024))
    local cursize =  string.format('%0.2f', (cur_process / 1024))
    local file_curt_dl_perct = (cur_process/file_len)*100
    
    file_curt_dl_perct = tonumber( string.format('%0.1f',file_curt_dl_perct) )

    set_value(screen_mmedia_dl, 6, file_curt_dl_perct)
    set_text(screen_mmedia_dl , 7, '下载进度: '..file_curt_dl_perct..' %')
    set_text(screen_mmedia_dl , 8, '文件大小: '..cursize..' KB / '..allsize..' KB')
end

--[[***************************************************************************
** Function name: on_http_download_file_cb
** Descriptions : 下载文件
** @key         : key, 屏幕    -> 4G模块的请求指令
** @value       : value,4G模块 -> 屏幕  的应答数据 
** @return      : nil, 无返回值
***************************************************************************--]]
function on_http_download_file_cb(key, value)
    my_print(key)
    my_uart_log('on_http_download_file_cb key: '..key..'\r\n')
    
    if key=='data_len' then
        file_len = value                                                        --获取文件长度
        open_state = file_open( HttpDownSave_FileTb[down_mode], over_write)     --创建文件
        if open_state~=true
        then
            uart_send_string(' open_state~=true\r\n ')
        else
            uart_send_string(' open_state==true\r\n ')
        end
        start_timer_download_speed(file_len)                                 --开始计算下载时间
        set_text( screen_mmedia_dl, 4, '')                                      --速度显示为空
    
    elseif key=='data'  --文件数据包   
    then
        my_write_filedata(HttpDownSave_FileTb[down_mode],value,add_write)     --分段写入数据
    
    elseif key=='finish' --下载结束
    then
        --down_file:close()
        start_timer(timerId_Sig_Weather, 5*1000 , 0, 0)                         --重新开启自动获取天气和信号强度功能
        file_close()                                                            --关闭文件
        file_len           = 0
        file_current_dl    = 0
        file_curt_dl_perct = 0
        set_value( screen_mmedia_dl, 6, 0)                                      --初始化进度条数值
        set_text( screen_mmedia_dl, 7, '下载进度：0%')                          --初始化进度文本数值
        set_text( screen_mmedia_dl , 8, '')
        set_visiable( screen_mmedia_dl, 6, 0)                                   --设置进度条不可见
        set_visiable( screen_mmedia_dl, 7, 0)                                   --设置进度文本不可见
        set_visiable( screen_mmedia_dl, 8, 0)                                   --设置文件大小文本不可见
        local dl_sec,dl_speed = stop_timer_download_speed()                     --停止下载计时，比返回总用时秒数，每秒下载速度
        set_text(screen_mmedia_dl,4,'下载速度：'..string.format('%0.2f',dl_speed)..'KB/s 总用时：'..dl_sec..'s ')
        air_set_baudrate(115200)                                                --重新设置4G模块和串口3波特率为115200
        
        --下载模式为1时显示图片
        if down_mode == 1
        then
            set_text(screen_mmedia_dl,5,httpDown_dl_process_end[down_mode])
            if surface ~= 0 
            then
                destroy_surface(surface) 
                surface = 0
            end
            picture_en=1
            surface = load_surface(HttpDownSave_FileTb[down_mode])
            redraw()
        
        --下载模式为2时播放视频
        elseif down_mode == 2
        then
            set_text(screen_mmedia_dl,5,httpDown_dl_process_end[down_mode])
            picture_en = 0
            stop_video()
            play_video(HttpDownSave_FileTb[down_mode], 40, 75, 480, 320)
            
        --下载模式为3时播放音乐
        elseif down_mode == 3
        then
            set_text(screen_mmedia_dl,5,httpDown_dl_process_end[down_mode])
            set_visiable(screen_mmedia_dl, 9, 1)                                --设置音乐图标显示
            picture_en = 0
            stop_video()
            stop_sound()
            play_sound(HttpDownSave_FileTb[down_mode])
        end
        
        set_value(screen_mmedia_dl    , 1, 0)
        set_value(screen_mmedia_dl    , 2, 0)
        set_value(screen_mmedia_dl    , 3, 0)
        set_enable(screen_mmedia_dl   , 1, 1)
        set_enable(screen_mmedia_dl   , 2, 1)
        set_enable(screen_mmedia_dl   , 3, 1)
   
    elseif key=='timeout' --下载超时
    then
        start_timer(timerId_Sig_Weather, 5*1000 , 0, 0)
        
        reset_4G()
        set_text( screen_mmedia_dl, 5, '下载超时，发生网络错误，重新设置中...')              --提示
        dl_reset_flag = 1
        
        air_set_baudrate(115200)                                                --重新设置4G模块和串口3波特率为115200
        stop_timer_download_speed()                                             --停止下载计时
    elseif key=='dl_file_read_retry' -- HTTPREAD 重下
    then
        --**********************************************************************
        --value：            retry: -1,601
        --要提取的值：       -1
        --                   601
        --正则表达式：       'retry: (%d*),(%d*)'
        --**********************************************************************
        local httpread_retry=''
        local status=''
        local regular_e = 'retry: (.*),(%d*)'                                  --正则表达式
        local httpread_retry,status = string.match(value,regular_e)             --获取的值赋给 
        air_log('user r retry: '..httpread_retry..','..status)
        if httpread_retry == '-1' and ( status == '408' or status == '601' or status == '603' )
        then
            reset_4G()
            set_text( screen_mmedia_dl, 5, '下载超时，发生网络错误，重新设置中...')          --提示
            dl_reset_flag = 1
        end
        air_set_baudrate(115200)                                                --重新设置4G模块和串口3波特率为115200
    elseif key=='dl_file_head_retry' -- HTTPHEAD 重下
    then
        --**********************************************************************
        --value：            retry: -1,601
        --要提取的值：       -1
        --                   601
        --正则表达式：       'retry: (%d*),(%d*)'
        --**********************************************************************
        local httphead_retry=''
        local status=''
        local regular_e = 'retry: (.*),(%d*)'                                  --正则表达式
        local httphead_retry,status = string.match(value,regular_e)             --获取的值赋给 
        air_log('user h retry: '..httpread_retry..','..status)
        if httphead_retry == '-1' and ( status == '408' or status == '601' or status == '603' )
        then
            reset_4G()
            set_text( screen_mmedia_dl, 5, '下载超时，发生网络错误，重新设置中...')          --提示
            dl_reset_flag = 1
        end
        air_set_baudrate(115200)                                                --重新设置4G模块和串口3波特率为115200
    end
end

--[[***************************************************************************
** Function name: on_draw
** Descriptions : 绘图函数
** @screen      : 当前页面
** @return      : nil,无返回值  
***************************************************************************--]]
function on_draw(screen)
    
    --显示下载的图片
    if screen == screen_mmedia_dl and picture_en==1 
    then
        --my_uart_log("Lua:debug >>> my_draw_sddisk_pic_and_name()\r\n")
        local show_x = 40
        local show_y = 75
        local show_w = 480 
        local show_h = 320
        local crop_x = 0
        local crop_y = 0
        draw_surface(surface, show_x, show_y, show_w, show_h, crop_x, crop_y)
    end
end

--[[***************************************************************************
** Function name: on_video_notify
** Descriptions : 视频回调函数
** @msg         : 播放通知，0-播放完，1-播放中
** @v1          : 当前播放时间，单位s，秒
** @v2          : 播放文件的总时间，单位s，秒  
** @return      : nil,无返回值 
***************************************************************************--]]
function on_video_notify(msg,v1,v2)
    my_print("Lua_debug : video notify msg -> "..msg.."  /  v1:v2 = "..v1..":"..v2)
    
    local token_time = ':'
    if msg == 1
    then
        local v1_h = string.format("%02d", math.modf(v1/60/60))
        local v1_m = string.format("%02d", math.modf((v1/60) % 60))
        local v1_s = string.format("%02d", math.modf(v1%60))
        local show_v1 = v1_h..token_time..v1_m..token_time..v1_s
        
        local v2_h = string.format("%02d", math.modf(v2/60/60))
        local v2_m = string.format("%02d", math.modf((v2/60) % 60))
        local v2_s = string.format("%02d", math.modf(v2%60))
        local show_v2 = v2_h..token_time..v2_m..token_time..v2_s

        --set_value(sc_video, 8, (v1/v2)*1000)
        --set_text(sc_video,  9, show_v1)
        --set_text(sc_video, 10, show_v2)
    elseif msg == 0
    then 
        --set_value(screen_mmedia_dl, 8, 0)
        --set_text(screen_mmedia_dl,  9, '')
        --set_text(screen_mmedia_dl, 10, '')
        play_video(HttpDownSave_FileTb[2], 40, 75, 480, 320)
    end
end

--[[***************************************************************************
** Function name: on_audio_callback
** Descriptions : 音乐回调函数
** @state       : 保留
** @return      : nil, 无返回值
***************************************************************************--]]
function on_audio_callback(state)
    play_sound(HttpDownSave_FileTb[down_mode])
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
        if ( control == 6 or value == 1 ) and value == 1
        then
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

----获取时间-----
--[[***************************************************************************
** Function name:  on_get_time_cb
** Descriptions :  获取时间回调函数
** @key         :  屏幕->4G模块的发送请求
** @value       :  4G模块->返回的数据
** @return      :  nil,无返回值
***************************************************************************--]]
function on_get_time_cb(key, value)
    
    my_print('on_get_time_cb key:'..key)
    my_uart_log('on_get_time_cb key:'..key..'\r\n')
    
    --**********************************************************************
    --value：            {"sysTime2":"2020-08-15 14:52:12","sysTime1":"20200815145212"}
    --要提取的值：       2020-08-15 14:52:12
    --对应正则表达式：       '"sysTime2":"(%d*)-(%d*)-(%d*) (%d*):(%d*):(%d*)"' 
    --**********************************************************************
    local string_time_ball=''
    local get_year, get_mon, get_day, get_hour, get_min, get_sec = 0
    local regular_e = '"sysTime2":"(%d*)-(%d*)-(%d*) (%d*):(%d*):(%d*)"'                    --正则表达式
    
    if key=='data' 
    then
        string_time_ball = table2str(value)                                              --表转换为字符串
        get_year, get_mon, get_day, get_hour, get_min, get_sec = string.match( string_time_ball, regular_e)--获取字符串中的时间
        set_date_time(get_year, get_mon, get_day, get_hour, get_min, get_sec)
    end
end

--[[***************************************************************************
** Function name: http_get_time
** Descriptions : 获取时间
** @return      : nil,无返回值
**                可以使用 'AT+CIPGSMLOC=1,1' 指令获取大致的北京时间，此方法得到的时间有可能有误差
***************************************************************************--]]
function http_get_time()
    my_print('http_get_time()')
    air_http_get(
        'http://quan.suning.com/getSysTime.do',
        on_get_time_cb)
end
----获取时间 end-----

----获取天气-----
------------获取当地经纬-----
--[[***************************************************************************
** Function name: get_local_latitude_longitude
** Descriptions : 获取当地经纬
** @return      : nil,无返回值
***************************************************************************--]]
function get_local_latitude_longitude()
    
    air_cmd_add('AT+CIPGSMLOC=1,1', 'OK', 9000)
end

------------获取当地经纬 end-----
------------百度API通过经纬度获取地址-----
--[[***************************************************************************
** Function name   : baiduAPI_get_local
** Descriptions    : 通过baiduAPI转换经纬度为具体地点
** local_latitude  : 纬度
** local_longitude : 经度
** @return         : nil,无返回值
***************************************************************************--]]
function baiduAPI_get_local(local_latitude,local_longitude)

    air_http_get(
        'http://api.map.baidu.com/reverse_geocoding/v3/?ak=IsOA0q3nApEHtpcojvh5k4GHNkKHhqT0&output=json&coordtype=wgs84ll&location='..local_latitude..','..local_longitude,
        baiduAPI_get_local_cb) 

end

--[[***************************************************************************
** Function name : my_find_ChinaShi
** Descriptions  : 转换城市名（UTF-8），如“广州市”转换为“广州”
** CityAddr      : 城市名，如“广州市”
** @return       : 城市名，如“广州”
***************************************************************************--]]
function my_find_ChinaShi(CityAddr)
   
    local strlen = string.len(CityAddr)
    local find_data  = 0
    local new_CityAddr = CityAddr
    
    for i = (strlen - 3), strlen
    do
        find_data = (find_data << 8) | string.byte(CityAddr, i, i)   --转换城市名字符串为16进制数
    end
    
    if (find_data & 0xFFFFFF) == 0xE5B882
    then
        new_CityAddr = string.sub(CityAddr, 1, (strlen - 3))    --转换城市名
    end
    
    return new_CityAddr
end

--[[***************************************************************************
** Function name:  baiduAPI_get_local_cb
** Descriptions:   baiduAPI请求的回调函数
**
**返回实例：
**string_time_ball:{"status":0,"result":{"location":{"lng":113.44028348580408,"lat":23.16279264489196},"formatted_address":"骞夸笢鐪佸箍宸炲競榛勫煍鍖哄瘜搴疯タ琛?0鍙?115","business":"绉戝?﹀??灏忔柊濉?,"addressComponent":{"country":"涓?鍥?","country_code":0,"country_code_iso":"CHN","country_code_iso2":"CN","province":"骞夸笢鐪?,"city":"骞垮窞甯?,"city_level":2,"district":"榛勫煍鍖?,"town":"","town_code":"","adcode":"440112","street":"瀵屽悍瑗胯??","street_number":"10鍙?115","direction":"闄勮繎","distance":"28"},"pois":[],"roads":[],"poiRegions":[],"sematic_description":"","cityCode":257}}
***************************************************************************--]]
function baiduAPI_get_local_cb(key,value)
    my_print('tcp_baiduAPI_get_local_cb key:'..key)
    my_uart_log('on_get_time_cb key:'..key..'\r\n')
    
    local string_time_ball=''
    
    if key=='data' then
        string_time_ball = table2str(value)                                 --表转换为字符串
        local jsondata = cjson.decode(string_time_ball)                     --json解析
            
        local local_city = jsondata['result']['addressComponent']["city"]   --提取表里的城市名（UTF-8）
        local_city = my_find_ChinaShi(local_city)                         --转换城市名
        http_get_wea_and_time(local_city)                                  --获取指定城市的天气和北京时间
    end
end
------------百度API通过IP获取地址 end-----

--推荐在网络上购买付费的天气API，如https://www.tianqiapi.com/index/doc?version=v61
--免费天气API，如https://www.tianqiapi.com/index/doc?version=v6
--xue、lei、shachen、wu、bingbao、yun、yu、yin、qing
local wea = { ['unknow']=0, ['xue']=1, ['lei']=2, ['shachen']=3, ['wu']=4, ['bingbao']=5, ['yun']=6,
                ['yu']=7, ['yin']=8, ['qing']=9, ['yunnight']=10, ['qingnight']=11 }
local wea_chinese = { ['unknow']='未知', ['xue']='雪', ['lei']='雷', ['shachen']='沙尘暴', ['wu']='有雾',
                    ['bingbao']='冰雹', ['yun']='多云', ['yu']='下雨', ['yin']='阴天', ['qing']='晴天',
                     ['yunnight']='多云', ['qingnight']='晴天' }
                  
--[[***************************************************************************
** Function name:  on_get_wea_cb
** Descriptions:   获取指定城市的天气和北京时间的回调函数
***************************************************************************--]]   
function on_get_wea_cb(key,value)
    my_print('on_get_wea_cb key:'..key)
    my_uart_log('on_get_wea_cb key:'..key..'\r\n')
    local jsonstring_wea_ball=''
    local jsondata_string = ''
    local city,wea_img,tem = 0,0,0
    
    if key=='data' then
        jsonstring_wea_ball = table2str(value)                  --表转换为字符串
        local jsondata = cjson.decode(jsonstring_wea_ball)      --解析JSON数据
        city = jsondata.city                                    --获取城市，UTF8编码
        wea_img = jsondata.wea_img                              --获取天气
        tem = jsondata.tem                                      --获取温度
        set_text(screen_main,7,city)                            --显示城市名（UTF-8）
        set_value(screen_main,10,wea[wea_img])                  --设置天气图标
        yearwea,monwea,daywea,hourwea,minwea,secwea,weekwea = get_date_time()  ---获取当前时间用于显示晚上的图标
        if wea_img == 'yun' or wea_img == 'qing' 
        then
            if hourwea <= 6 or hourwea >= 19 
            then
                if wea_img == 'yun' 
                then
                    set_value(screen_main,10,wea['yunnight'])   --显示晚上多云的图标
                end
                if wea_img == 'qing' 
                then
                    set_value(screen_main,10,wea['qingnight'])  --显示晚上晴的图标
                end
            end
        end
        set_text(screen_main,9,wea_chinese[wea_img])            --设置天气中文名称
        set_text(screen_main,8,tem)                             --设置温度
        set_text(screen_main,16,'')
        set_visiable(screen_main, 16, 0)
    end
end

--[[***************************************************************************
** Function name:  http_get_wea_and_time
** Descriptions:   获取指定城市的天气和北京时间
***************************************************************************--]] 
function http_get_wea_and_time(local_city)
    my_print('http_get_wea_and_time()')
    my_uart_log('http_get_wea_and_time \r\n')
    if local_city~=nil 
    then
        set_text(screen_main,16,'正在获取天气...')
        my_print('http_get_time()')
        http_get_time()
        --my_print('ip: '..local_city)
        air_http_get(
            'https://tianqiapi.com/api?version=v61&appid=37231943&appsecret=2BJ3teqj&city='..local_city,
            on_get_wea_cb,
            'USER_DEFINED',
            'ntent-Type: application/x-www-form-urlencoded'
            )
    end
end

--[[***************************************************************************
** Function name:  get_wea_and_time
** Descriptions:   调用此函数开始获取天气,首先获取经纬度
**
** 步骤：
**      1、获取经纬度
**      2、获取经纬度所代表的城市
**      3、获取天气和北京时间
***************************************************************************--]]
function get_wea_and_time()
    get_local_latitude_longitude()
end
----获取天气 end-----

--[[***************************************************************************
** Function name : at_cops_csq
** Descriptions  : 获取运营商信息、信号强度
** @return       : nil,无返回值
***************************************************************************--]]
function at_cops_csq()
    air_cmd_add('AT+COPS?','OK',1000)
    air_cmd_add('AT+CSQ','OK',1000)
    on_air_resp_other_callback = at_cops_csq_cb   ----用户自定义的4G模块-数据回调函数
end
--在on_air_resp_callback()中处理返回的信息
----获取运营商信息 end-----

--[[***************************************************************************
** Function name:  air_get_iccid
** Descriptions :  获取SIM卡的 ICCID 号码 
***************************************************************************--]]
function air_get_iccid()
    air_cmd_add('AT+ICCID','OK',1000,3)
end

--下载计时的变量
local sys_timer_sec_num=0
local sys_timer_sec=0
local sys_timer_min=0
local dl_speed=0
local speed_http_file_size=0
--[[***************************************************************************
** Function name : on_systick
** Descriptions  : 系统回调函数，系统每隔1秒钟自动调用
** @return       : nil,无返回值
***************************************************************************--]]
function on_systick()
    
    if sys_timer_en==1
    then
        --开始计时
        sys_timer_sec_num=sys_timer_sec_num+1
        sys_timer_sec=sys_timer_sec_num%60
        sys_timer_min=math.floor(sys_timer_sec_num/60)
        --text='用时:'..sys_timer_min..':'..sys_timer_sec
        --set_text(0,6,text)
    else
        --停止计时
        if sys_timer_sec_num~=0 
        then
            dl_speed =  ( speed_http_file_size /1024 )/sys_timer_sec_num 
            --text='用时: '..sys_timer_min..':'..sys_timer_sec..'\r\n'..'speed: '..dl_speed..' KB/min\r\n'
            --set_text(0,6,text)
        end
        sys_timer_sec_num=0
    end
    
end

--[[***************************************************************************
** Function name:  start_timer_download_speed
** Descriptions:   开始计时
***************************************************************************--]]
function start_timer_download_speed(filesize)
    sys_timer_en=1
    speed_http_file_size = filesize
end

--[[***************************************************************************
** Function name:  stop_timer_download_speed
** Descriptions:   停止计时
** Back:           sys_timer_sec_num    下载总秒数
**                 dl_speed             下载速度KB/s
***************************************************************************--]]
function stop_timer_download_speed()
    sys_timer_en=0
    dl_speed =  ( speed_http_file_size /1024 )/sys_timer_sec_num
    return sys_timer_sec_num,dl_speed
end

--[[***************************************************************************
** Function name : on_control_notify
** Descriptions  : 系统回调函数，用户通过触摸修改控件后，执行此回调函数。
**                 点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
** @return       : nil,无返回值
***************************************************************************--]]
function on_control_notify(screen,control,value)
    
    --***************************************************************************
    --位置：    主界面
    --***************************************************************************
    if screen == screen_main 
    then
        --连接Aliyun
        if control==123 and value==1 and mqtt_sub_bit=='not_connect' 
        then
            aliyun_get_iot_token()
        end
    end
    
    --***************************************************************************
    --位置：    空调控制器
    --***************************************************************************
    if screen == screen_ac_control 
    then
        if      control==1
             or    control==3
             or control==6
             or control==7
             or control==10
        then
            --屏发布消息
            aliyun_pub(screen,control,value)
        end
    end
    
    --***************************************************************************
    --位置：    多媒体下载
    --***************************************************************************
    if screen == screen_mmedia_dl 
    then
        if (control >= 1 and control <= 3) and value == 1
        then
            stop_sound()
            stop_video()
            
            set_enable( screen_mmedia_dl, 1, 0)
            set_enable( screen_mmedia_dl, 2, 0)
            set_enable( screen_mmedia_dl, 3, 0)
            
            set_value( screen_mmedia_dl, 6, 0)
            set_text( screen_mmedia_dl , 7, '')
            set_text( screen_mmedia_dl , 8, '')
            set_text( screen_mmedia_dl, 4, '')
            
            set_visiable( screen_mmedia_dl, 6, 1)           --设置进度条可见
            set_visiable( screen_mmedia_dl, 7, 1)           --设置进度文本可见
            set_visiable( screen_mmedia_dl, 8, 1)           --设置文件大小文本可见
            
            set_visiable( screen_mmedia_dl, 9, 0)            --设置音乐图标隐藏
            
            picture_en = 0
            file_len           = 0
            file_current_dl    = 0
            file_curt_dl_perct = 0
            
            air_set_baudrate(921600)
            down_mode = control
            set_text( screen_mmedia_dl, 5, httpDown_dl_process[down_mode])
            stop_timer( timerId_Sig_Weather)                 --停止自动获取天气、时间、信号强度
            
            --开始下载
            air_http_download(HttpDownFile_URITb[down_mode], 
                              7168,
                              on_http_download_file_cb)
        
        --退出界面时停止播放视频或音频
        elseif control == 111 and value == 0
        then
            stop_sound()
            stop_video()
        end
    end
    
    --***************************************************************************
    --位置：    4G模块测试
    --***************************************************************************
    if screen == screen_4gtest 
    then
        --开始测试
        if control==1 and value == 1 
        then
            set_text( screen_4gtest, 2, '')
            set_text( screen_4gtest, 3, '')
            set_text( screen_4gtest, 4, '')
            set_text( screen_4gtest, 5, '')
            set_enable(screen_4gtest, 1, 0)
            at_test()
            
        --退出
        elseif control==111 and value==1 
        then
            set_text( screen_4gtest, 2, '')
            set_text( screen_4gtest, 3, '')
            set_text( screen_4gtest, 4, '')
            set_text( screen_4gtest, 5, '')
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
** Descriptions:   接收串口3的数据的回调函数。串口3是连接4G模块。
***************************************************************************--]]
function on_uart_recv_data3(packet)
    on_air_recv_data(packet)
end

----出厂测试-----

--[[***************************************************************************
** Function name : on_test_time_cb
** Descriptions  : 复位测试的回调函数。
**                 处理复位时获取的信息，用于判断通讯是否成功
** @return       : nil,无返回值
***************************************************************************--]]
function on_test_time_cb(key,value)
    my_print('on_test_time_cb key:'..key)
    my_uart_log('on_test_time_cb key:'..key..'\r\n')
    
    local string_time_ball=''
    
    --**********************************************************************
    --value：        {"sysTime2":"2020-08-15 14:52:12","sysTime1":"20200815145212"}
    --**********************************************************************
    if key=='data' 
    then
        string_time_ball = table2str(value)      --表转换为字符串
        if string.find(string_time_ball,'sysTime2')~=nil
        then
            set_text( screen_4gtest, 4, '通讯成功！！！')
            set_fore_color( screen_4gtest, 4, 0x34CC )
            
            reset_4G()                                              --通讯成功，开始复位4G
            test_reset_bit = 'reset_4G'                         --测试复位标志位
        end
    end
end

--[[***************************************************************************
** Function name :  at_test
** Descriptions  :   复位测试函数
** @return       : nil,无返回值
***************************************************************************--]]
function at_test()
    
    stop_timer(timerId_Sig_Weather)                             --停止自动获取天气、时间、信号强度
    reset_4G_READY=0
    test_reset_bit = ''
    air_cmd_add('AT+CSQ','OK',1000)                             --获取信号强度
    --网络通讯测试，获取时间
    air_http_get(
        'http://quan.suning.com/getSysTime.do',
        on_test_time_cb
    )
end
----出厂测试 end-----


