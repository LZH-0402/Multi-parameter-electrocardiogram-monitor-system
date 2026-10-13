--下面列出了常用的回调函数
--更多功能请阅读<<物联型LUA脚本API.pdf>>

local open_debug_uatr = 1

local FA_OPEN_EXISTING = 0x00
local FA_READ          = 0x01 --读
local FA_WRITE         = 0x02 --写
local FA_CREATE_NEW    = 0x04 --创建新文件
local FA_CREATE_ALWAYS = 0x08 --覆盖创建新文件
local FA_OPEN_ALWAYS   = 0x10 --打开

local WriteOnceSize = 2048
local add_write     = 0
local over_write    = 0

local sc_Home       = 0
local sc_ReadFlile  = 1
local sc_WriteFile  = 2
local sc_prompt     = 3
local sd_dir = ''
local cnt = 0

local sc_ShowRecord = 0
local IsinsertSD    = 0

--debug serial port output debugging information
--@debugInfo: debug information
function my_debug_print(debugInfo)
	if open_debug_uatr == 1
	then
		print(debugInfo)
	end
end

--split string function
--@str: string to be split
--@pat: delimiter
function my_split(str, pat) 

	local i = 1
	local t = {}
	local last_end = 0
	local s, e = string.find(str, pat, 1)
	
	while s 
	do	
		table.insert(t, string.sub(str, last_end + 1, last_end + s - last_end - 1))
		last_end = e
		s, e = string.find(str, pat, last_end + 1)
		
		i = i + 1
	end
	
	if last_end <= #str
	then
		cap = string.sub(str, last_end + 1)
		table.insert(t, cap)
	end
	
	return t
end

--get the type and length of the variable
--@data: 'string' or 'table'
--@return: len and type of data
function my_getdataLen_Type(data)
	
	my_debug_print('---------- my_getdataLen_Type ----------')

	local datalen = -1
	--获取数据类型
	local data_type = type(data)
	
	--计算数据长度
	if data_type == 'string'
	then
		datalen = string.len(data)
		
	elseif data_type == 'table'
	then
		datalen = #(data)
	end
	
	my_debug_print('Lua_debug data Type and Len: '..data_type..' / '..datalen)
	
	return datalen,data_type
end

--display file data on the record
--@mystr: file data
function my_ShowRecord_FromFile(mystr)
	
	my_debug_print('---------- my_ShowRecord_FromFile ----------')
	
	local line_data = my_split(mystr, '%\n')
	my_debug_print('Lua_debug: line_data -> '..#(line_data))
	
	for i = 1, #(line_data)
	do
		if line_data[i] ~= nil 
		then
			feed_dog()
			
			line_data[i] = string.gsub(line_data[i], "%\r", '')
			line_data[i] = string.gsub(line_data[i], "%\n", '')
			line_data[i] = line_data[i]..';'
			--my_debug_print('Lua_debug: '..i..'th -> '..line_data[i])
			record_add(sc_ShowRecord, 1, '$NUM;'..line_data[i])	
		end
	end
end

--read file data
--@file:file path
function my_read_filedata(file)
	my_debug_print('---------- my_read_filedata ----------')
	my_debug_print('Lua_debug: file -> '..file)

	local count    = 0
	local read_cnt = 0
	local offset    = 0
	local all_byte = 0

	local read_byte_Tb = {}
	local read_char_Tb = {}

	local read_str     = ''
	local open_state = file_open(file, FA_READ)
	if open_state == true
	then
		--获取当前文件大小
		all_byte = file_size()
		if all_byte > 0
		then
			read_cnt =  math.modf(all_byte/WriteOnceSize)
			if all_byte % WriteOnceSize > 0
			then
				read_cnt = read_cnt + 1
			end
			my_debug_print('Lua_debug: need read allcnt -> '..read_cnt)
			
			for i = 1, read_cnt
			do
				--复位读字节数组
				read_byte_Tb = {}
				read_char_Tb = {}
				
				--计算读取的偏移位置
				offset = (i - 1) * WriteOnceSize
				local offst_result = file_seek(offset)
				--文件偏移失败
				if offst_result == false 
				then
					set_text(sc_prompt, 1, 'When reading the file, an offset error occurred. please try again! ! !')
					set_text_roll(sc_prompt, 1, 50)
					change_child_screen(sc_prompt)
					
					break
				end
				my_debug_print('Lua_debug: cur offset  -> '.. offset)

				--计算本次读的个数
				count = WriteOnceSize
				if i == read_cnt
				then
					if all_byte % WriteOnceSize > 0
					then
						count = all_byte % WriteOnceSize
					end
				end
				my_debug_print('Lua_debug: cur read  -> '..i..'th / wrire count '..count)
				
				--读取字节转换为字符并拼接成字符串
				read_byte_Tb = file_read(count)
				if #(read_byte_Tb) > 0
				then
					for j = 0, #(read_byte_Tb)
					do
						read_char_Tb[j + 1] = string.char(read_byte_Tb[j])
					end
					read_str = read_str..table.concat(read_char_Tb)
				elseif read_byte_Tb ==nil
				then
					set_text(sc_prompt, 1, ' File read error. please try again! ! !')
					set_text_roll(sc_prompt, 1, 50)
					change_child_screen(sc_prompt)
					break;
				end
			end
			my_ShowRecord_FromFile(read_str)
			
		else
			set_text(sc_prompt, 1, 'The file don`t exist, please check the contents of the SD car! ! !')
			set_text_roll(sc_prompt, 1, 50)
			change_child_screen(sc_prompt)
		end
	else
		--关闭文件
		file_close()
	end
end


--Write data to the end of the file
--@file:file path
--@data:The type of '@data' only be [string] or [byte array]
--@open_mode:open file mode
function my_write_filedata(file, data, open_mode)
	
	my_debug_print('---------- my_write_filedata ----------')
	my_debug_print('Lua_debug: file -> '..file..' / data -> '..type(data)..' / open_mode -> '..open_mode)
	
	local count     = 0
	local write_cnt = 0
	local seek_ops  = 0
	local all_byte  = 0
	
	--获取待写入数据的数据类型和长度
	local wrire_len, data_type = my_getdataLen_Type(data)
	
	local write_byte_Tb = {}
	local open_state = file_open(file, open_mode)
	
	if open_state == true
	then
		--获取当前文件大小,仅在追加文件末尾写入执行
		if open_mode == add_write
		then
			all_byte = file_size()
		end
		
		if wrire_len > 0
		then
			--计算'@data'要写多少次
			write_cnt =  math.modf(wrire_len / WriteOnceSize)
			if wrire_len % WriteOnceSize > 0
			then
				write_cnt = write_cnt + 1
			end
			my_debug_print('Lua_debug: need write allcnt -> '..write_cnt)
			
			for i = 1, write_cnt
			do
				--复位写字节数组
				write_byte_Tb = {}
				
				--计算写的位置
				seek_ops = (i - 1) * WriteOnceSize +all_byte
				file_seek(seek_ops)
				--文件偏移失败
				if offst_result == false 
				then
					set_text(sc_prompt, 1, 'When writing the file, an offset error occurred. please try again! ! !')
					set_text_roll(sc_prompt, 1, 50)
					change_child_screen(sc_prompt)
					break
				end
				my_debug_print('Lua_debug: cur seek_ops  -> '..seek_ops)
				
				--计算本次写的个数
				count = WriteOnceSize
				if i == write_cnt
				then
					if wrire_len % WriteOnceSize > 0
					then
						count = wrire_len % WriteOnceSize
					end
				end
				my_debug_print('Lua_debug: cur write  -> '..write_cnt..'th / wrire count '..count)
				
				--填充写入flash的字节数组
				for j = 1, count 
				do
					if data_type == 'string'
					then
						--字符串类型，将每个字符转换为字节数组
						write_byte_Tb[j - 1] = tonumber(string.byte(data, ((i - 1) * WriteOnceSize + j), ((i - 1) * WriteOnceSize + j)))
						
					elseif data_type == 'table'
					then
						--数组类型，字节赋值
						write_byte_Tb[j - 1] = data[((i - 1) * WriteOnceSize + j)]
					end
				end
				
				local IswriteOK = file_write(write_byte_Tb)
				if IswriteOK == false
				then
					i = i - 1
				end	
			end
		end
		
	else
		set_text(sc_prompt, 1, 'The file don`t exist, please check the contents of the SD car! ! !')
		set_text_roll(sc_prompt, 1, 50)
		change_child_screen(sc_prompt)	
	end
	
	--关闭文件
	file_close()
end


--初始化函数
function on_init()
	add_write  = FA_WRITE|FA_READ
	over_write = FA_CREATE_ALWAYS|FA_WRITE
end

--定时回调函数，系统每隔1秒钟自动调用。
function on_systick()
end

--用户通过触摸修改控件后，执行此回调函数。
--点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
function on_control_notify(screen,control,value)
	
	if screen == sc_ReadFlile and control == 2 and value == 0
	then
		sc_ShowRecord = sc_ReadFlile
		
		if IsinsertSD == 1
		then
			record_clear(sc_ReadFlile, 1)
			my_read_filedata(sd_dir..'/'..'1.txt')
		else
			set_text(sc_prompt, 1, 'Please insert the SD card, or check if the SD is compatible! ! !')
			set_text_roll(sc_prompt, 1, 50)
			change_child_screen(sc_prompt)
		end

	elseif screen == sc_WriteFile and control == 3 and value == 0
	then
		sc_ShowRecord = sc_WriteFile
		
		if IsinsertSD == 1
		then
			record_clear(sc_WriteFile, 1)
			
			local str = '\n'..get_text(sc_WriteFile, 2)    
			my_write_filedata(sd_dir..'/'..'1.txt', str, add_write)
			
			my_read_filedata(sd_dir..'/'..'1.txt')
			local allrecordcnt = record_get_count(sc_WriteFile, 1)
			record_setoffset(sc_WriteFile, 1, allrecordcnt - 1)
			record_select(sc_WriteFile   , 1, allrecordcnt - 1)	
		else
			set_text(sc_prompt, 1, 'Please insert the SD card, or check if the SD is compatible! ! !')
			set_text_roll(sc_prompt, 1, 50)
			change_child_screen(sc_prompt)
		end
		
		
	elseif screen == sc_WriteFile and control == 5 and value == 0
	then
		sc_ShowRecord = sc_WriteFile
		
		if IsinsertSD == 1
		then
			record_clear(sc_WriteFile, 1)
			cnt = cnt + 1
			local str = (cnt + 1)..'th write -> '..get_text(sc_WriteFile, 4) 
			my_write_filedata(sd_dir..'/'..'NewTxtFile.txt', str, over_write)
			
			my_read_filedata(sd_dir..'/'..'NewTxtFile.txt')
		else			
			set_text(sc_prompt, 1, 'Please insert the SD card, or check if the SD is compatible! ! !')
			set_text_roll(sc_prompt, 1, 50)
			change_child_screen(sc_prompt)
		end
		
	elseif screen == sc_prompt and control == 3 and value == 0
	then
		change_child_screen(sc_ShowRecord)
	end
	
end

--当画面切换时，执行此回调函数，screen为目标画面。
function on_screen_change(screen)
end

--插入SD卡回调函数
function on_sd_inserted(dir)
	
	IsinsertSD = 1
	sd_dir = dir
	
end

--拔掉SD卡回调函数
function on_sd_removed()
	
	IsinsertSD = 0
	
	record_clear(sc_ReadFlile, 1)
	record_clear(sc_WriteFile, 1)
end
