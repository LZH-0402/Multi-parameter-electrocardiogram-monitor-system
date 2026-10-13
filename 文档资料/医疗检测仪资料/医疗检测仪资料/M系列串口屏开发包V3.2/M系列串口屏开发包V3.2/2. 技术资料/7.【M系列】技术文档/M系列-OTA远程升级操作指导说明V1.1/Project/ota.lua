--MANN+ HUMMEL LEADERSHIP IN FILRATION 
--ota updata

--start updata 
--mcu -> screen : 

local sc_Lock = 0


local ota_code      = 0x88
local _start_down   = 0x11
local _dwoning      = 0x22
local _end_down     = 0x33
local _cancel_down  = 0x44
local _file_check   = 0x55
local _unzip        = 0x66

ota_suc              = 0x80
ota_nodown           = 0x81
ota_snfail           = 0x82
ota_packet_checkfail = 0x83

local TransferSize   = 0
local filesize       = 0x00000000

local filename          = 'ota.bin'
local downfile_path     = '1:/ota.bin'
local sd_path           = '1:/ota.bin'
local otaUartPacketSize = 512
local pre_sn            = -1
local down_state        = 0
local ota_addr          = 0

local ota_file_md5   = '0123456789abcdef'
local ota_addr_start = 128*1024*1024
local ota_cnt        = 0
local ota_writeTb    = {}
local ota_writeByte  = 0

--ota down to sd : file read / write
local en_ota_SD        = 0
local open_state       = 0
local FA_OPEN_EXISTING = 0x00
local FA_READ          = 0x01     --only read
local FA_WRITE         = 0x02     --only write
local FA_CREATE_NEW    = 0x04     --creat new file
local FA_CREATE_ALWAYS = 0x08     --over creat file
local FA_OPEN_ALWAYS   = 0x10     --open
local add_write        = FA_WRITE|FA_READ           --read & write
local over_write       = FA_CREATE_ALWAYS|FA_WRITE  --over creat file & write

local baudrate = 115200

--get the type and length of the variable
--@data: 'string' or 'table'
--@return: len and type of data
function my_getdataLen_Type(data)
    
    local datalen = -1
    --get data type:string or table
    local data_type = type(data)
    
    --calculate data lenght
    if data_type == 'string'
    then
        datalen = string.len(data)
        
    elseif data_type == 'table'
    then
        datalen = #(data)
    end
    
    return datalen,data_type
end

--Write data to the end of the file
--@file:file path
--@data:The type of '@data' only be [string] or [byte array]
--@open_mode:open file mode
function my_write_filedata(file, data, open_mode)
    
    local count     = 0
    local write_cnt = 0
    local seek_ops  = 0
    local all_byte  = 0
    
    --get data lenght and type
    local wrire_len, data_type = my_getdataLen_Type(data)
    
    local write_byte_Tb = {}

    if open_state == true
    then
        --Write at the end of the file
        if open_mode == add_write
        then
            all_byte = file_size()
        end
        
        if wrire_len > 0
        then
            --count the number of writes
            write_cnt =  math.modf(wrire_len / otaUartPacketSize)
            if wrire_len % otaUartPacketSize > 0
            then
                write_cnt = write_cnt + 1
            end
            
            for i = 1, write_cnt
            do
                write_byte_Tb = {}
                
                --calculate write location
                seek_ops = (i - 1) * otaUartPacketSize + all_byte
                file_seek(seek_ops)
                
                --calculate the current number of writes
                count = otaUartPacketSize
                if i == write_cnt
                then
                    if wrire_len % otaUartPacketSize > 0
                    then
                        count = wrire_len % otaUartPacketSize
                    end
                end
                
                for j = 0, count 
                do
                    --string type:char to number
                    if data_type == 'string'
                    then
                        write_byte_Tb[j - 1] = tonumber(string.byte(data, ((i - 1) * otaUartPacketSize + j), ((i - 1) * otaUartPacketSize + j)))
                        
                    --table type
                    elseif data_type == 'table'
                    then
                        write_byte_Tb[j] = data[((i - 1) * otaUartPacketSize + j)]
                    end
                end
                
                local IswriteOK = file_write(write_byte_Tb)
                if IswriteOK == false
                then
                    i = i - 1
                end    
            end
        end
    end
end

--checksum calculation
--@arr   : table
--@return: 1-suc, 0-fail
function my_checkSum(arr)
    
    ----EE B6 LEN(2BYTE) 88 22 01 03 E8 packet check_H check_L FF FC FF FF
    local result = 0x0000     
    local tmp    = 0x0000
    
    for i = 1, (#(arr) - 6)
    do
        tmp = (tmp + arr[i]) & 0xFFFF
    end
    tmp = 0xFFFF - tmp
    
    if tmp == ((arr[#(arr) - 5] << 8) | (arr[#(arr) - 4]))
    then
        result = 1  
        --print('> checkSum('..string.format('%02X',tmp)..') == packet('..string.format('%02X', ((arr[#(arr)-5]<<8)|(arr[#(arr)-4])))..')')
    else
        result = 0
        --print('> checkSum('..string.format('%02X',tmp)..') ~= packet('..string.format('%02X', ((arr[#(arr)-5]<<8)|(arr[#(arr)-4])))..')')
    end
    
    return result
end

--ota answer
--@downType: 0x11-start, 0x22-downing, 0x33-end, 0x44-cancel down
--@code    : see top
--@sn      : 
function my_uartsend_ota(downType, code, sn)
    
    local ota_buff = {}
    
    ota_buff[0] = 0xEE
    ota_buff[1] = 0xA6
    
    ota_buff[2] = downType
    ota_buff[3] = code
    
    if sn ~= nil
    then
        ota_buff[4] = sn
    end
    
    ota_buff[#(ota_buff) + 1] = 0xFF
    ota_buff[#(ota_buff) + 1] = 0xFC
    ota_buff[#(ota_buff) + 1] = 0xFF
    ota_buff[#(ota_buff) + 1] = 0xFF
    
    uart_send_data(ota_buff)
end

--sd update ota 
function my_sd_updataOTA()
    
	local finish = 0
	
    if file_open(sd_path, 1) == true 
    then
        size = file_size()
		ota_destroy()
        
        local sdota_addr = ota_init('0123456789abcdef', size, 0x800000)     

        --ota resume
        if sdota_addr > 0 
        then
            file_seek(sdota_addr)
        end
        
        while(true) 
        do
            local rddata = file_read(2048)
            if #(rddata) < 2047
			then
				for i = (#(rddata) + 1), 2047
				do
					rddata[i] = 0x00

				end
				finish=1
			end

			ota_write(rddata)      
			print('> finish = '..finish)
			if finish == 1 then
				break
			end
        end

        file_close()
		my_assecc_ota(1)
        ota_check_upgrade(1)
    end
end

--uart ota transmission operation
function ota_operating(packet)

    local datalen = (packet[2] << 8) | packet[3]
    local funcode = packet[4] 
    local cur_screen = get_current_screen()
    
    if funcode == ota_code
    then
        --EE B6 LEN(2BYTE) 88 11 FILESIZE(4byte) FILENAME FF FC FF FF
        local child_code = packet[5]
        local down_name  = ''
        --print('> child_code = ' ..string.format('%02X', child_code))
            
        if child_code == _start_down
        then
            baudrate = uart_get_baudrate()
            
            local baudrateTemp = 115200
            
            baudrateTemp = (packet[6] << 24) | (packet[7] << 16) | 
                           (packet[8] <<  8) | (packet[9] <<  0)

            filesize = (packet[10] << 24) | (packet[11] << 16) | 
                       (packet[12] <<  8) | (packet[13] <<  0)
                    
            for i = 14, (#(packet) - 4)
            do
                down_name = down_name..string.char(packet[i])
            end
            print('> baudrateTemp = '..baudrateTemp ..' / filesize = ' ..string.format('%08X', filesize) ..' / filename = '..down_name)
            
            if down_name == filename and filesize > 0
            then
                my_assecc_ota(1)
                down_state = 1
                change_screen(sc_Lock)
                
                my_uartsend_ota(child_code, ota_suc)
                
                if en_ota_SD == 1
                then
                    open_state = file_open(downfile_path, over_write)
                    if open_state == true
                    then
                        print(' > open_state yes ')
                    else
                        print(' > open_state no ')
                    end
                else
                    pre_sn     = -1
                    ota_cnt        = 0
                    ota_writeTb    = {}
                    ota_writeByte  = 0
      
                    TransferSize   = 0

                    set_value(sc_Lock,  88, 0)
                    set_text(sc_Lock, 188, 'Downloading : 0 %   [ '..math.ceil(TransferSize)..' byte / '..math.ceil(filesize)..' byte ]')
                    
					if filesize > 0
					then
						print('> 写入文件大小 = '..filesize)
						ota_destroy() 
						ota_addr = ota_init('0123456789abcdef', filesize, 0x800000)
					end
                    
                end        
                uart_set_baudrate(baudrateTemp)    
            else
                my_uartsend_ota(child_code, ota_nodown)            
            end
      
        elseif child_code == _dwoning
        then
            --EE B6 LEN(2BYTE) 88 22 01 00 00 11 11 check_H check_L FF FC FF FF
            local sn = packet[6]
            
            if pre_sn == 255
            then
                pre_sn = -1
            end
            print(' > pre_sn = '..pre_sn..' / sn = '..sn)

            if pre_sn + 1 == sn
            then
                
                local packsize = #(packet) - 13 + 1
                
                if packsize > 0 and packsize <= otaUartPacketSize
                then

                    if ((packsize + 13) == (#(packet) + 1)) and (((#(packet) + 1) - 13) > 0)
                    then
                        local isCheckTure = my_checkSum(packet)
                        
                        if isCheckTure == 1
                        then
                            TransferSize = packsize + TransferSize
                            local prg = string.format ('%.1f', ((TransferSize * 100) / filesize))
                            set_value(sc_Lock,  88, math.ceil((TransferSize * 1000) / filesize))
                            set_text(sc_Lock, 188, 'Downloading : '..prg..' %   [ '..math.ceil(TransferSize)..' byte / '..math.ceil(filesize)..' byte ]')
                            --print(' > down = ' ..prg..' %   [ '..math.ceil(TransferSize)..' byte / '..math.ceil(filesize)..' byte ]')
                            
                            for i = 7, (#(packet) - 6)
                            do
                                ota_writeTb[ota_writeByte] = packet[i]
                                ota_writeByte = ota_writeByte + 1
                            end
                            
                            if #(ota_writeTb) >= 0
                            then 
                            
                                local isOtaWrite = 0
                                
                                ota_cnt = ota_cnt + 1
                                if ota_cnt == 4 
                                then
                                    isOtaWrite = 1
                                    
                                elseif ota_cnt < 4 and TransferSize == filesize
                                then
                                    isOtaWrite = 1
                                end
                                
                                if isOtaWrite == 1
                                then
                                    
                                    if #(ota_writeTb) <= 2047
                                    then
                                        for i = (#(ota_writeTb) + 1), 2047
                                        do
                                            ota_writeTb[ota_writeByte] = 0x00
                                            ota_writeByte = ota_writeByte + 1
                                        end
                                    end
                                    
                                    
                                    if en_ota_SD == 1
                                    then
                                        my_write_filedata(downfile_path, ota_writeTb ,add_write) 
                                    end

                                    --ota resume
                                    if ota_addr > 0 
                                    then
                                        file_seek(ota_addr)
                                    end  
                                    
                                    local _write =  ota_write(ota_writeTb)
                                    
                                    ota_cnt = 0
                                    ota_writeByte = 0 
                                    ota_writeTb   = {}
                                end
                                
                                pre_sn = sn
                                my_uartsend_ota(child_code, ota_suc, pre_sn)
                            end   
                        end
                    else
                        --(packsize + 12) ~= (#(packet) + 1)
                        print(' > cmd lenght ERROR !! data ('.. packsize ..') + 13 != cmd lenght('..(#(packet) + 1)..')' )
                        my_uartsend_ota(child_code, ota_packet_checkfail, (pre_sn + 1))
                    end
                else
                    --size > 1000 error
                    print(' > packet size > '..otaUartPacketSize..' ERROR !')
                end
            
            else
                my_uartsend_ota(child_code, ota_snfail, (pre_sn + 1))
            end     
            
        elseif child_code == _end_down and down_state == 1
        then
            local isUpdata = packet[6]
            if isUpdata == 0x01
            then
                my_uartsend_ota(child_code, ota_suc)
                
                if en_ota_SD == 1
                then
                    --close file
                    file_close()
                else
                    ota_check_upgrade(1) 
                end
            end

        elseif child_code == _cancel_down and down_state == 1
        then
            if en_ota_SD == 1
            then
                --close file
                file_close()
            else
                ota_destroy()
            end
            
            down_state = 0
            my_assecc_ota(0)
            my_uartsend_ota(child_code, ota_suc)
        end
    end
end

function my_assecc_ota(state)

    pre_sn         = -1
    ota_cnt        = 0
    ota_writeTb    = {}
    ota_writeByte  = 0
    TransferSize   = 0
    
    set_value(sc_Lock,  88, 0)
    set_text(sc_Lock, 188, 'Downloading : 0 %   [ '..math.ceil(TransferSize)..' byte / '..math.ceil(filesize)..' byte ]')
    
    set_visiable(sc_Lock,  88, state)
    set_visiable(sc_Lock, 188, state)
    
    set_enable(sc_Lock, 1, ((state + 1) % 2))
    set_enable(sc_Lock, 2, ((state + 1) % 2))
    set_enable(sc_Lock, 3, ((state + 1) % 2))
end

function on_ota_progress(status,value)
    
    if status == 1 and value == 0
    then
        set_value(sc_Lock, 88, value)
        set_text(sc_Lock, 188, '')
        refresh_screen()
        
    elseif status == 2
    then
        my_uartsend_ota(_file_check, value)
        
    elseif status == 3
    then
        set_value(sc_Lock, 88, value*10)
        set_text(sc_Lock, 188, 'flash_updatesects : '..value..' %')
        refresh_screen()
        
    elseif status == 4
    then
        
        my_uartsend_ota(_unzip, value)
        print('> baudrate = '..baudrate)
        uart_set_baudrate(baudrate)    
        delay_ms(100)
    end
end