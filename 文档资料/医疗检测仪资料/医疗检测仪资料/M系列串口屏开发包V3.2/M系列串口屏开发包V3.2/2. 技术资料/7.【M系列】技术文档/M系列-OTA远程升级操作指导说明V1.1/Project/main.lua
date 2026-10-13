
-------------------------------------------------------------------------------
-------------------------- screen id define strat ----------------------------- 
-------------------------------------------------------------------------------

--screen id variant: define rule 'sc_' + name

sc_Lock              = 0
-------------------------------------------------------------------------------
--------------------------- screen id define end ------------------------------ 
-------------------------------------------------------------------------------

function on_init()
    
    dofile('ota.lua')
    my_assecc_ota(0)
	print('> init ok !!!')
end

function on_uart_recv_data(packet)
	
    collectgarbage("collect")
    local isOTA = packet[1]
    
   -- print('> funcode = '..string.format('%02X', packet[2])) 
    
    if isOTA == 0xB6
    then
        ota_operating(packet)
	end
end

function on_sd_inserted(dir)
	my_sd_updataOTA()
end