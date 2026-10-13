--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:                main.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:                4G例程，获取天气、时间；获取运营商、信号强度；
**
**--------------------------------------------------------------------------
** Created By:                林绍佳
** Created date:            2020/08/21
** Version:                    V1.0    
** Descriptions:            4G例程，获取天气、时间；获取运营商、信号强度；
**
**--------------------------------------------------------------------------
** Modified by:
** Modified date:
** Version:
** Description:
**
***************************************************************************--]]

--[[
** 友情提示：本例程中使用的 HTTP 公网接口的介绍如下方链接：
** 1、百度地图逆地址解析 API 
**  http://lbsyun.baidu.com/index.php?title=webapi/guide/webservice-geocoding-abroad
** 2、济南易客云网络科技提供天气查询 API 
**  付费： https://www.tianqiapi.com/index/doc?version=v61
**  免费： https://www.tianqiapi.com/index/doc?version=v6
** 3、如要获取时间，请自行查找稳定连接
**  http://quan.suning.com/getSysTime.do
--]]

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

--画面ID
local screen_main = 0

--[[***************************************************************************
** Function name:  my_print
** Descriptions:   打印虚拟屏调试信息，设置 en_debug = 1
                   注意：实体屏运行时，必须设置 en_debug = 0
***************************************************************************--]]
local en_debug_msg = 2 --0-不显示调试信息;1-虚拟屏; 2-实体屏，串口1输出

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
** Function name:  air_get_iccid
** Descriptions :  获取SIM卡的 ICCID 号码 
***************************************************************************--]]
function air_get_iccid()
    air_cmd_add('AT+ICCID','OK',1000,3)
end

--[[***************************************************************************
** Function name:  on_init
** Descriptions:   系统初始化时，执行此回调函数。
                   注意：回调函数的参数和函数名固定不能修改
***************************************************************************--]]
function on_init()
    
    dofile('air724at.lua')                                                      --加载 http.lua 文件
    uart_set_baudrate3(115200)                                              --设置与4G模块通讯的串口3的波特率为115200
    
    air_set_callback(on_air_send_cb,on_air_resp_callback,on_air_log_cb)     --设置4G库函数的命令发送函数，命令回调函数、调试信息打印函数
    
    air_hw_int()                                                            --4G模块初始化设置
    air_get_iccid()
    
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
  record_add(0,100,string.sub(msg,1,100))
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
        set_text(screen_main, 16, '')
        at_cops_csq()
        get_wea_and_time()
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
        local regular_e = '+ICCID: (%d*)'                   --正则表达式
        local my_iccid = string.match( value, regular_e)      --获取的值赋给 my_iccid 
        set_text( screen_main, 3, 'SIM卡号ICCID：'..my_iccid)
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
	--条件：    获取经纬度成功
	--功能：    订阅主题
	--***************************************************************************
    if string.find(value,'+CIPGSMLOC') ~= nil
    then
        --**********************************************************************
        --value：            +CIPGSMLOC: 0,31.241045,121.472313,18/11/08,15:37:30
        --要提取的值：       31.241045
        --                   121.472313
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
    
end

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
    
    --**********************************************************************
    --value：            {"sysTime2":"2020-08-15 14:52:12","sysTime1":"20200815145212"}
    --要提取的值：       2020-08-15 14:52:12
    --对应正则表达式：   '"sysTime2":"(%d*)-(%d*)-(%d*) (%d*):(%d*):(%d*)"' 
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
** 注意         : 本例程中使用的获取时间的连接，可能会失效，请自行搜索可靠连接
**                可以使用 'AT+CIPGSMLOC=1,1' 指令获取大致的北京时间，此方法得到的时间有可能有误差
***************************************************************************--]]
function http_get_time()
    my_print('http_get_time()')
    
    --本例程中使用的获取时间的连接，可能会失效，请自行搜索可靠连接
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
	
    air_cmd_add('AT+CIPGSMLOC=1,1', 'OK', 5000)
end

------------获取当地经纬 end-----

------------百度API通过经纬度获取地址-----
--[[***************************************************************************
** Function name   : baiduAPI_get_local
** Descriptions    : 通过baiduAPI转换经纬度为具体地点
** local_latitude  : 经度
** local_longitude : 纬度
** @return         : nil,无返回值
** 注意            : 本例程中使用的百度API的key/ak可能失效，
**                   请在百度地图API上注册可靠的key/ak
**                   测试时，在key/ak属性中，设置IP白名单为0.0.0.0，即可使用
** 推荐的地图API   : 百度地图API逆地址解析：http://lbsyun.baidu.com/index.php?title=webapi/guide/webservice-geocoding-abroad
***************************************************************************--]]
function baiduAPI_get_local(local_latitude,local_longitude)
    
    --本例程中使用的百度API的key/ak可能失效，请在百度地图API上注册可靠的key/ak
    air_http_get(
        'http://api.map.baidu.com/reverse_geocoding/v3/?ak=IsOA0q3nApEHtpcojvh5k4GHNkKHhqT0&output=json&coordtype=wgs84ll&location='..local_latitude..','..local_longitude,
        baiduAPI_get_local_cb) 

end

--[[***************************************************************************
** Function name : my_find_ChinaShi
** Descriptions  : 转换城市名（UTF-8），如’广州市‘转换为’广州
** CityAddr      : 经度
** @return       : nil,无返回值
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
*****************************************************************************--]]
function baiduAPI_get_local_cb(key,value)
    my_print('tcp_baiduAPI_get_local_cb key:'..key)
    
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

--[[*******************************************************************
--本例程中使用的天气API的key可能失效，请自行搜索可靠的天气API
--推荐在网络上购买付费的天气API，如济南易客云网络科技提供HTTP天气访问接口
--如https://www.tianqiapi.com/index/doc?version=v61
--免费天气API，如https://www.tianqiapi.com/index/doc?version=v6
--
--xue、lei、shachen、wu、bingbao、yun、yu、yin、qing
*******************************************************************--]]
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
** 本例程中使用的天气API的key可能失效，请自行搜索可靠的天气API
** 推荐在网络上购买付费的天气API，如济南易客云网络科技提供HTTP天气访问接口,
** 如https://www.tianqiapi.com/index/doc?version=v61
** 免费天气API，如https://www.tianqiapi.com/index/doc?version=v6
***************************************************************************--]] 
function http_get_wea_and_time(local_city)
    my_print('http_get_wea_and_time()')
   
    if local_city~=nil 
    then
        set_text(screen_main,16,'正在获取天气...')
        my_print('http_get_time()')
        http_get_time()
        --my_print('ip: '..local_city)
        --本例程中使用的天气API可能失效，请自行搜索可靠的天气API
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
**步骤：
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
