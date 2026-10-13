

--[[*****************************Copyright(c)**********************************
**                Guangzhou DACAIGAUNGDIAN Technology Co. Ltd
**
**                             http://www.gz-dc.com
**
**------File Info-----------------------------------------------------------
** File Name:               http.lua
** Latest modified Date:    2020/08/21
** Latest Version:            
** Description:             指纹模块LUA函数库。对指令进行封装。
**
**--------------------------------------------------------------------------
** Created By:             
** Created date:            2020/08/21
** Version:                 V1.0    
** Descriptions:            指纹模块LUA函数库。对指令进行封装。
**
**--------------------------------------------------------------------------
** Modified by:
** Modified date:
** Version:
** Description:
**
***************************************************************************--]]
    
local at_timer_id = 31              --发送AT的超时定时器
local function_code = 0

--[[***************************************************************************
** Function name:  my_http_uart_log
** Descriptions:   打印实体屏调试信息，设置 http_en_debug = 2
                   注意：虚拟屏运行时，http_en_debug 不能等于 2
***************************************************************************--]]
function my_http_uart_log(debug_msg)

     uart_send_string(debug_msg)

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
** @send_cb     :  向指纹发送数据的回调函数
** @resp_cb     :  所有指纹返回的数据的回调函数
** @log_cb      :  调试信息回调函数
***************************************************************************--]]
function air_set_callback(send_cb,resp_cb,log_cb)
  send_callback = send_cb
  resp_callback = resp_cb
  log_callback = log_cb
end

-----------------对指纹模块的命令发送队列 BEGIN--------------------------
--对指纹模块的命令发送队列
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
----------------对指纹模块的命令发送队列 END--------------------------------

local cmd_list = List.new()         --命令队列
local cmd_currunt = nil             --当前发送的命令

--[[***************************************************************************
** Function name:  air_cmd_add
** Descriptions :  命令添加到队列，当前没有已发送的命令时，发送添加的第一条命令
** @sendstr     :  发送的命令
** @ackstr      :  指纹模块对命令的返回的数据
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
    local data_type = type(cmd_currunt.send)
	if data_type == 'string'
	then
		if string.len(cmd_currunt.send)>1
		then
			air_log('OUT: '..cmd_currunt.send)
			
			send_callback(cmd_currunt.send)
			
			send_callback('\r\n')
		
		end
	elseif data_type == 'table'
	then
		if (#(cmd_currunt.send)+1) > 0
		then
			function_code = cmd_currunt.send[9]
			send_callback(cmd_currunt.send)
		end
	end
    --如果命令不为空，则发送命令
    
    
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
** Descriptions :  指纹模块返回数据处理
** @resp        ： 指纹模块返回的数据
***************************************************************************--]]
function cmd_list_check_ack(resp)  
    if resp[6] ==  nil then
		
	else
		resp_callback(function_code,resp) 		
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
** Descriptions :  指纹模块定时处理接口，在 maiin.lua 的 on_timer 中处理
***************************************************************************--]]
function on_air_timer(timer_id)
    if timer_id==at_timer_id then
        cmd_list_timeout()                      -- 超时计时器
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

local recv_buffer2_count = 0
local recv2_buffer       = {}
local cmd_head           = 0
local cmd_flag           = 0

--[[***************************************************************************
** Function name  :  on_fingerprint_recv_data
** Descriptions   :  对指纹模块串口数据回调
** @ packet    :  串口数据
***************************************************************************--]]
function on_fingerprint_recv_data(packet)
	local count = #(packet)
	collectgarbage("collect")

	local check_data         = 0
	local check_head         = 0XEF01FFFFFFFF
	local data_length        = 0
	for i = 0,(#(packet)) do
		cmd_head = (cmd_head<<8)|packet[i]
		cmd_head = cmd_head&0xffffffffffff
		
		if cmd_head==check_head then   
			recv2_buffer = {}
			recv_buffer2_count = 6

			recv2_buffer[0] = 0xEF
			recv2_buffer[1] = 0x01
			recv2_buffer[2] = 0xFF
			recv2_buffer[3] = 0xFF
			recv2_buffer[4] = 0xFF
			recv2_buffer[5] = 0xFF
			cmd_flag = 1
		else
			recv2_buffer[recv_buffer2_count] = packet[i]
			recv_buffer2_count = recv_buffer2_count+1
		end
		if cmd_flag == 1 then
			if recv_buffer2_count >= 10 then                --指令长度判定
	            data_length = (recv2_buffer[7]<<8) | (recv2_buffer[8]&0XFF)	
					if recv_buffer2_count >= data_length + 9 then
						cmd_list_check_ack(recv2_buffer) 
						cmd_flag = 0 
						recv2_buffer = {}
						recv_buffer2_count = 0
						break
					end
					
			end
			
		end

	end


end

--[[***************************************************************************
** Function name:  my_addcheckSum
** Descriptions :  和校验
***************************************************************************--]]
function my_addcheckSum(arr,flag)
    
    ----EE B6 LEN(2BYTE) 88 22 01 03 E8 packet check_H check_L FF FC FF FF
    local result = 0x0000     
    local tmp    = 0
    for i = 6, (#(arr)-flag)
    do

        tmp = (tmp + arr[i]) & 0xFFFF
    end

    return tmp
end

--[[***************************************************************************
** Function name:  lib_Automatic_registration
** Descriptions :  注册指纹
** @ count    :    注册次数
** @ save_id    :  存储序号（序号在0~99范围内，如果输入0X1234 则自动排序）
***************************************************************************--]]
function lib_Automatic_registration(count,save_id)    --注册指纹

	local command_buffer = {}
	command_buffer[0] = 0xEF
	command_buffer[1] = 0x01
	command_buffer[2] = 0xFF
	command_buffer[3] = 0xFF
	command_buffer[4] = 0xFF
	command_buffer[5] = 0xFF

	command_buffer[6] = 0x01	
	command_buffer[7] = 0x00
	command_buffer[8] = 0x08

	command_buffer[9] = 0x54

	command_buffer[10] = 0xFF
	
	command_buffer[11] = 0xF0 | (count&0x0F)
	command_buffer[12] = save_id>>8
	command_buffer[13] = save_id&0XFF
	command_buffer[14] = 0x00
	local add_vlaue = my_addcheckSum(command_buffer,0)

	command_buffer[15] = add_vlaue>>8

	command_buffer[16] =  add_vlaue&0XFF
	air_cmd_add(command_buffer, 0xFF, 1000)
end

--[[***************************************************************************
** Function name:  lib_Automatic_retrieval
** Descriptions :  自动检索指纹（检索范围0~99）
** @ start_id    :  起始序号
** @ end_id    :    结束序号
***************************************************************************--]]
function lib_Automatic_retrieval(start_id,end_id) 
	local command_buffer = {}
	command_buffer[0] = 0xEF
	command_buffer[1] = 0x01
	command_buffer[2] = 0xFF
	command_buffer[3] = 0xFF
	command_buffer[4] = 0xFF
	command_buffer[5] = 0xFF

	command_buffer[6] = 0x01	
	command_buffer[7] = 0x00
	command_buffer[8] = 0x08

	command_buffer[9] = 0x58

	command_buffer[10] = 0xFF
	
	command_buffer[11] = start_id>>8
	command_buffer[12] = start_id&0XFF
	command_buffer[13] = end_id>>8
	command_buffer[14] = end_id&0XFF
	
	local add_vlaue = my_addcheckSum(command_buffer,0)

	command_buffer[15] = add_vlaue>>8

	command_buffer[16] =  add_vlaue&0XFF
	air_cmd_add(command_buffer,  0xFF, 300)
	
end

--[[***************************************************************************
** Function name:  lib_delete_number
** Descriptions :  批量删除指纹
** @ start_id    :  起始序号
** @ end_count    :  删除数量
***************************************************************************--]]
function lib_delete_number(start_id,end_count)  --删除指纹个数  
	local command_buffer = {}
	command_buffer[0] = 0xEF
	command_buffer[1] = 0x01
	command_buffer[2] = 0xFF
	command_buffer[3] = 0xFF
	command_buffer[4] = 0xFF
	command_buffer[5] = 0xFF

	command_buffer[6] = 0x01	
	command_buffer[7] = 0x00
	command_buffer[8] = 0x08

	command_buffer[9] = 0x0C
	
	command_buffer[10] = start_id>>8
	command_buffer[11] = start_id&0XFF
	command_buffer[12] = end_count>>8
	command_buffer[13] = end_count&0XFF
	command_buffer[14] = 0x00
	local add_vlaue = my_addcheckSum(command_buffer,0)

	command_buffer[15] = add_vlaue>>8

	command_buffer[16] =  add_vlaue&0XFF
	air_cmd_add(command_buffer, recv_Tb, 1000)
end


--[[***************************************************************************
** Function name:  lib_read_number
** Descriptions :  读取指纹个数
***************************************************************************--]]
function lib_read_number()   
	local command_buffer = {}
	command_buffer[0] = 0xEF
	command_buffer[1] = 0x01
	command_buffer[2] = 0xFF
	command_buffer[3] = 0xFF
	command_buffer[4] = 0xFF
	command_buffer[5] = 0xFF

	command_buffer[6] = 0x01	
	command_buffer[7] = 0x00
	command_buffer[8] = 0x03
	command_buffer[9] = 0x1D

	local add_vlaue = my_addcheckSum(command_buffer,0)

	command_buffer[10] = add_vlaue>>8

	command_buffer[11] =  add_vlaue&0XFF
	air_cmd_add(command_buffer, recv_Tb, 1000)
end


--[[***************************************************************************
** Function name:  lib_clear_fingerprint
** Descriptions :  清空指纹库
***************************************************************************--]]
function lib_clear_fingerprint()    --清空指纹库
	
	local command_buffer = {}
	command_buffer[0] = 0xEF
	command_buffer[1] = 0x01
	command_buffer[2] = 0xFF
	command_buffer[3] = 0xFF
	command_buffer[4] = 0xFF
	command_buffer[5] = 0xFF

	command_buffer[6] = 0x01	
	command_buffer[7] = 0x00
	command_buffer[8] = 0x03
	command_buffer[9] = 0x0D

	local add_vlaue = my_addcheckSum(command_buffer,0)

	command_buffer[10] = add_vlaue>>8

	command_buffer[11] =  add_vlaue&0XFF
	--uart_send_data(command_buffer)
	air_cmd_add(command_buffer, recv_Tb, 1000)
	
end


--[[***************************************************************************
** Function name:  lib_get_device
** Descriptions :  获取设备信息
***************************************************************************--]]
function lib_get_device()         --获取设备信息
	
	local command_buffer = {}
	command_buffer[0] = 0xEF
	command_buffer[1] = 0x01
	command_buffer[2] = 0xFF
	command_buffer[3] = 0xFF
	command_buffer[4] = 0xFF
	command_buffer[5] = 0xFF

	command_buffer[6] = 0x01	
	command_buffer[7] = 0x00
	command_buffer[8] = 0x04
	command_buffer[9] = 0x47
	command_buffer[10] = 0x01
	local add_vlaue = my_addcheckSum(command_buffer,0)

	command_buffer[11] = add_vlaue>>8
	command_buffer[12] = add_vlaue&0XFF
	
	--uart_send_data(command_buffer)
	air_cmd_add(command_buffer, recv_Tb, 1000)
	
end



--[[***************************************************************************
** Function name:  lib_stop_prograss
** Descriptions :  终止进程
***************************************************************************--]]
function lib_stop_prograss()  
	
	local command_buffer = {}
	command_buffer[0] = 0xEF
	command_buffer[1] = 0x01
	command_buffer[2] = 0xFF
	command_buffer[3] = 0xFF
	command_buffer[4] = 0xFF
	command_buffer[5] = 0xFF

	command_buffer[6] = 0x01	
	command_buffer[7] = 0x00
	command_buffer[8] = 0x03
	command_buffer[9] = 0xAA

	local add_vlaue = my_addcheckSum(command_buffer,0)

	command_buffer[10] = add_vlaue>>8
	command_buffer[11] = add_vlaue&0XFF
	--uart_send_data(command_buffer)
	air_cmd_add(command_buffer, recv_Tb, 1000)
	
end
