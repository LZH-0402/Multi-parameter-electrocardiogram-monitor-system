--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:                main.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:                4G例程，HTTP下载大文件并保存到屏的内存中；
**
**--------------------------------------------------------------------------
** Created By:                林绍佳
** Created date:            2020/08/21
** Version:                    V1.0    
** Descriptions:            4G例程，HTTP下载大文件并保存到屏的内存中；
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
local mobile_MCCMNC       = { ['46000']='中国移动', ['46001']='中国联通', ['46002']='中国移动', ['46003']='中国电信'}    --移动编号，用于显示当前运营商
local setup_mobile_mccmnc = { ['46000']='CMIOT', ['46001']='uninet', ['46002']='CMIOT', ['46003']='emome'}         --移动接入点，用于设置网络接入点
local my_mobile_MCCMNC    = ''

--定时获取天气和信号强度的回调函数的定时器
local timerId_Sig_Weather = 0
local timer0_notify_cnt   = 0

--画面ID
local screen_mmedia_dl = 0

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
   -- air_get_iccid()
    
    start_timer(timerId_Sig_Weather, 1000 , 0, 0)                           --开启自动获取型号强度、时间、天气功能
    
    --4G 初始化中，初始化为不允许用户点击下载
	set_enable(screen_mmedia_dl, 1, 0)
	set_enable(screen_mmedia_dl, 2, 0)
	set_enable(screen_mmedia_dl, 3, 0)
	
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
        if timer0_notify_cnt%15 == 0
        then
            --每15s调用一次，更新信号值
            at_cops_csq()
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
        set_enable(screen_mmedia_dl, 1, 1)
		set_enable(screen_mmedia_dl, 2, 1)
		set_enable(screen_mmedia_dl, 3, 1)
		at_cops_csq()
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
        set_text( screen_mmedia_dl, 11, mobile_MCCMNC[my_mobile_MCCMNC])
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
			--set_text( screen_mmedia_dl, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'2G')
			set_value( screen_mmedia_dl, 10, 1)                       --设置信号图标显示第1帧 
        elseif my_csq>=12 and my_csq<=13 
        then
            --set_text( screen_mmedia_dl, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'2G')
			set_value( screen_mmedia_dl, 10, 2)                       --设置信号图标显示第2帧 
        elseif my_csq>=14 and my_csq<=15 
        then
            --set_text( screen_mmedia_dl, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'3G')
			set_value( screen_mmedia_dl, 10, 3)                       --设置信号图标显示第3帧 
        elseif my_csq>=16 
        then
            --set_text( screen_mmedia_dl, 11, mobile_MCCMNC[my_mobile_MCCMNC]..'4G')
			set_value( screen_mmedia_dl, 10, 4)                       --设置信号图标显示第4帧 
        end
        my_print(my_csq)
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
local httpDown_dl_process_end = {'图片名称：image.jpg','视频名称：bench.mp4', '音乐名称：my dream.mp3'}
local HttpDownSave_FileTb     = {'3:/image.jpg','3:/bench.mp4', '3:/my dream.mp3'}
local HttpDownFile_URITb      = {'http://video.gz-dc.com/logo_480320_165kV2.jpg',
                                 'http://video.gz-dc.com/video2_62M9s.mp4',
                                 'http://video.gz-dc.com/music503k32s.mp3'}

local dl_reset_flag=0             --http下载文件错误处理，重启
--[[***************************************************************************
** Function name: my_getdataLen_Type
** Descriptions : 获取待写入数据的数据类型和长度
** @data        : 代写入的数据
** @return      : datalen,数据大小; data_type,数据类型
***************************************************************************--]]
function my_getdataLen_Type(data)

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
             
            for i = 1, write_cnt
            do
                --复位写字节数组
                write_byte_Tb = {}
                
                --计算写的位置
				seek_ops = (i - 1) * WriteOnceSize +all_byte
				
				--显示当前写入（下载）进度
                http_download_process(seek_ops)
                
				--文件偏移失败
				file_seek(seek_ops)
                if offst_result == false 
                then
                    break
                end
                
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
                
                local IswriteOK = file_write(write_byte_Tb)
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
    set_text(screen_mmedia_dl , 8, '文件大小: '..cursize..' K / '..allsize..' K')
end

--[[***************************************************************************
** Function name: on_http_download_file_cb
** Descriptions : 下载文件的对调函数，可以自定义函数名
** @key         : key, HTTP响应数据类型
** @value       : value,HTTP响应的数据
** @return      : nil, 无返回值
**
** 若key = ‘data_len’，表示HTTP下载的文件大小；value 表示为文件大小，单位byte。
** 若key = ‘data’，表示HTTP响应数据内容；value为数据内容，类型为字节数组，大小为air_http_download(url,break_size,user_callback)设置的break_size。
** 若key = ‘finish’，表示HTTP下载完成；value为文件总大小，单位byte
** 若key = ‘timeout’，表示HTTP下载超时。
** 若key = ‘dl_file_head_retry’，表示查询 HTTP 头信息超时。若超时，内部已经处理了3次重发数据请求。
** 若key = ‘dl_file_read_retry’，表示查询 HTTP 读取数据超时。若超时，内部已经处理了3次重发数据请求。
**
***************************************************************************--]]
function on_http_download_file_cb(key, value)
    my_print(key)
   
    if key=='data_len' then
        file_len = value                                                        --获取文件长度
        open_state = file_open( HttpDownSave_FileTb[down_mode], over_write)     --创建文件
        if open_state~=true
        then
            uart_send_string(' open_state~=true\r\n ')
        else
            uart_send_string(' open_state==true\r\n ')
        end
        start_timer_download_speed(file_len)                                    --开始计算下载时间
        set_text( screen_mmedia_dl, 4, '')                                      --速度显示为空
    
	elseif key=='data'  --文件数据包   
    then
        my_write_filedata(HttpDownSave_FileTb[down_mode],value,add_write)       --分段写入数据
    
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
        local dl_sec,dl_speed = stop_timer_download_speed()                     --停止下载计时，比返回总用时秒数，每秒下载速度
        set_text(screen_mmedia_dl,4,'下载速度：'..string.format('%0.2f',(dl_speed))..'KB/s  总用时间：'..dl_sec..' s')
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
	    
		set_text(screen_mmedia_dl  , 5, '下载超时，请重新下载！')
		dl_reset_flag = 1
        set_value(screen_mmedia_dl , 1, 0)
        set_value(screen_mmedia_dl , 2, 0)
        set_value(screen_mmedia_dl , 3, 0)
        set_enable(screen_mmedia_dl, 1, 1)
        set_enable(screen_mmedia_dl, 2, 1)
        set_enable(screen_mmedia_dl, 3, 1)
        
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
            set_text( screen_mmedia_dl, 5, '下载超时，发送网络错误，重新设置中...')          --提示
            dl_reset_flag = 1
        end
		
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
            set_text( screen_mmedia_dl, 5, '下载超时，发送网络错误，重新设置中...')          --提示
            dl_reset_flag = 1
        end	
		
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
        local v2_m = string.format("%02d",  math.modf((v2/60) % 60))
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
** Function name : on_audio_callback
** Descriptions  : 声音播放结束回调通知
** @state        : 保留未使用。
** @return       : nil,无返回值
***************************************************************************--]]
function on_audio_callback (state)
	play_sound(HttpDownSave_FileTb[down_mode])
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

--下载计时的变量
local sys_timer_sec_num    = 0
local sys_timer_sec        = 0
local sys_timer_min        = 0
local dl_speed             = 0
local speed_http_file_size = 0
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
        end
        sys_timer_sec_num=0
    end
    
end

--[[***************************************************************************
** Function name:  start_timer_download_speed
** Descriptions:   开始计时
***************************************************************************--]]
function start_timer_download_speed(filesize)
    sys_timer_en = 1
    speed_http_file_size = filesize
end

--[[***************************************************************************
** Function name:  stop_timer_download_speed
** Descriptions:   停止计时
** Back:           sys_timer_sec_num    下载总秒数
**                 dl_speed             下载速度KB/s
***************************************************************************--]]
function stop_timer_download_speed()
    sys_timer_en = 0
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
    --位置：    多媒体下载
    --***************************************************************************
    if screen == screen_mmedia_dl 
	then
        
        if (control >= 1 and control <= 3) and value == 1
        then
            stop_sound()
            stop_video()
            
            set_enable(screen_mmedia_dl, 1, 0)
            set_enable(screen_mmedia_dl, 2, 0)
            set_enable(screen_mmedia_dl, 3, 0)
            
            set_visiable(screen_mmedia_dl,6,1)              --设置进度条可见
            set_visiable(screen_mmedia_dl,7,1)              --设置进度文本可见
            
            set_visiable(screen_mmedia_dl, 9, 0)            --设置音乐图标隐藏
            
            picture_en = 0
            file_len           = 0
            file_current_dl    = 0
            file_curt_dl_perct = 0
            
            air_set_baudrate(921600)
            down_mode = control
            set_text(screen_mmedia_dl,5,httpDown_dl_process[down_mode])
           -- stop_timer(timerId_Sig_Weather)                 --停止自动获取天气、时间、信号强度
            
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
