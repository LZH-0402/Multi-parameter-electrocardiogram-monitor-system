--下面列出了常用的回调函数
--更多功能请阅读<<LUA脚本API.pdf>>
local sc_main2    = 0
local sc_music    = 1
local sc_video    = 2

local video_Tb      = {}
local video_cnt     = 0
local video_curpage = 1
local video_allpage = 1
local paly_video_index = 0

local music_Tb      = {}
local music_cnt     = 0
local music_curpage = 1
local music_allpage = 1
local paly_music_index = 0

local list_token = '/'

local play_color   = 0x067F
local noplay_color = 0x8CF1
local roll_speed   = 50

local sd_dir      = ''

local video_timer  = 1
local volume_timer = 2

local video_x = 353
local video_y = 64
local video_w = 400
local video_h = 320 

function my_set_volume_visiable(state)
	for i = 37, 41
	do
		set_visiable(sc_music, i, state)
		set_visiable(sc_video, i, state)
	end
	
	if state == 1
	then
		start_timer(volume_timer, 2*1000, 1, 1)
	end
end

function my_set_volume_value(value)
	
	if value == 0 
	then
		set_value(sc_music, 35, 0)
		set_value(sc_music, 38, 0)
			
		set_value(sc_video, 35, 0)
		set_value(sc_video, 38, 0)
		
	elseif value > 0 and value <= 33
	then
		set_value(sc_music, 35, 1)
		set_value(sc_music, 38, 1)
		
		set_value(sc_video, 35, 1)
		set_value(sc_video, 38, 1)
	
	elseif value > 34 and value <= 66
	then
		set_value(sc_music, 35, 2)
		set_value(sc_music, 38, 2)
		
		set_value(sc_video, 35, 2)
		set_value(sc_video, 38, 2)
	
	elseif value > 67 and value <= 100
	then
		set_value(sc_music, 35, 3)
		set_value(sc_music, 38, 3)
		
		set_value(sc_video, 35, 3)
		set_value(sc_video, 38, 3)
	end
	
	set_text(sc_music,41, value)
	set_text(sc_video,41, value)
	
	set_volume(value)
end

function my_set_music_playgif()
	
	stop_animation(sc_music, 31)
	stop_animation(sc_music, 32)
	
	play_animation(sc_music, 31)
	
end

function my_setvisable_video_progress(state)
	
	print("---------- my_setvisable_video_progress ----------")
	print("Lua_debug state -> "..state)
	set_visiable(sc_video, 22, state)
	set_visiable(sc_video, 23, state)
	set_visiable(sc_video, 24, state)
	
	set_visiable(sc_video, 26, state)
	set_visiable(sc_video, 27, state)
	set_visiable(sc_video, 28, state)

	if state == 1
	then
		stop_timer(video_timer)
		start_timer(video_timer, 2*1000, 1, 1)
	end
	
	
end

function my_set_music_playstate(xth_music)
	
	local cur_musicname_len = 0
	
	if xth_music > 0
	then
		local xth_line = xth_music % 5
	
		if xth_line == 0
		then
			xth_line = 5
		end
		
		for i = 1, 5
		do
			cur_musicname_len = string.len(get_text(sc_music, i + 5))
			if i == xth_line
			then
				set_value(sc_music, i, 1)
				set_fore_color(sc_music, i + 5, play_color)
				if cur_musicname_len > 16
				then
					set_text_roll(sc_music, i + 5, roll_speed) 
				end
			else
				set_value(sc_music, i, 0)
				set_fore_color(sc_music, i + 5, noplay_color)
				set_text_roll(sc_music, i + 5, 0) 
			end
		end
		
	else
		for i = 1, 5
		do
			set_value(sc_music, i, 0)
			set_fore_color(sc_music, i + 5, noplay_color)
			set_text_roll(sc_music, i + 5, 0) 
		end
	end
	
end

function my_show_cur_musicpage(xth_musicpage)
	
	print("---------- my_show_cur_musicpage ----------")
	print("Lua_debug : music current page ->"..xth_musicpage)
	print("Lua_debug : music allcnWEt -> "..music_cnt)
	
	if music_cnt > 0 
	then
		local music_idnex = (xth_musicpage - 1) * 5 
		
		for i = 1, 5 
		do
			if (music_idnex + i) <= music_cnt 
			then
				local show_music_name = string.match(music_Tb[music_idnex + i], ".+/(.+)")	 
				set_text(sc_music, (5 + i), show_music_name)   
				set_visiable(sc_music, (0 + i), 1)
				set_enable(sc_music, (10 + i), 1)                   	
			else
				set_text(sc_music, (5 + i), ' ')
				set_visiable(sc_music, (0 + i), 0)
				set_enable(sc_music, (10 + i), 0)    
			end
		end
		
		if paly_music_index > 0
		then
			local cur_play_page = math.modf(paly_music_index / 5)
			if paly_music_index % 5 > 0
			then
				cur_play_page = cur_play_page + 1
			end
			
			if cur_play_page == xth_musicpage
			then
				my_set_music_playstate(paly_music_index)
			
			else
				my_set_music_playstate(0)
			end
		end
		
	end  
end

function my_set_video_playstate(xth_video)
	
	local cur_videoname_len = 0
	
	if xth_video > 0
	then
		local xth_line = xth_video % 5
		if xth_line == 0
		then
			xth_line = 5
		end
		
		for i = 1, 5
		do
			cur_videoname_len = string.len(get_text(sc_video, i + 5))
			if i == xth_line
			then
				set_value(sc_video, i, 1)
				set_fore_color(sc_video, i + 5, play_color)
				if cur_videoname_len > 12
				then
					set_text_roll(sc_video, i + 5, roll_speed) 
				end
			else
				set_value(sc_video, i, 0)
				set_fore_color(sc_video, i + 5, noplay_color)
				set_text_roll(sc_video, i + 5, 0) 
			end
		end
		
	else
		for i = 1, 5
		do
			set_value(sc_video, i, 0)
			set_fore_color(sc_video, i + 5, noplay_color)
			set_text_roll(sc_video, i + 5, 0) 
		end
	end
	
end

function my_show_cur_videopage(xth_videopage)
	
	print("---------- my_show_cur_videopage ----------")
	print("Lua_debug : video current page ->"..xth_videopage)
	print("Lua_debug : video allcnt -> "..video_cnt)
	
	if video_cnt > 0 
	then
		local video_idnex = (xth_videopage - 1) * 5 
		
		for i = 1, 5 
		do
			if (video_idnex + i) <= video_cnt 
			then
				local show_video_name = string.match(video_Tb[video_idnex + i], ".+/(.+)")	 
				print("Lua_debug : show_video_name -> "..show_video_name)
				
				set_text(sc_video, (5 + i), show_video_name)   
				set_visiable(sc_video, (0 + i), 1)
				set_enable(sc_video, (10 + i), 1)                   	
			else
				set_text(sc_video, (5 + i), ' ')
				set_visiable(sc_video, (0 + i), 0)
				set_enable(sc_video, (10 + i), 0)    
			end
		end
		
		if paly_video_index > 0
		then
			local cur_play_page = math.modf(paly_video_index / 5)
			if paly_video_index % 5 > 0
			then
				cur_play_page = cur_play_page + 1
			end
			
			if cur_play_page == xth_videopage
			then
				my_set_video_playstate(paly_video_index)
			else
				my_set_video_playstate(0)
			end
		end
		
	end  
end

-- 获取扩展名 
-- 自定义函数（非回调函数，可以修改）
-- @str 需要获取后缀名的字符串
function my_get_extension(str)  
	local file_type = ''
	local token_s, token_e = string.find(str, "%.", 1)
	
	if token_s ~= nil
	then
		file_type = string.sub(str, (token_s + 1), string.len(str)) 
		print("file_type -> "..file_type)
	end
	
	return file_type
end

--@path    :文件路径
--@filename:文件名称
--@type    :0 文件夹，1 文件
--@fsize   :文件大小
function on_list_dir(path, filename, type, fsize)
	
	print("---------- on_list_sddir ----------")
	
	--print("path -> "..path)
	--print("filename -> "..filename)
	--print("type -> "..type)
	--print("fsize -> "..fsize)
	
	if type == 1
	then
		local cur_file_type = my_get_extension(filename)
		--print("cur_file_type -> "..cur_file_type)
		if cur_file_type == 'mp4'
		then
			set_enable(sc_video, 23, 1)
			video_cnt = video_cnt + 1
			video_Tb[video_cnt] = path..list_token..filename
		
		elseif cur_file_type == 'MP4'
		then
			set_enable(sc_video, 23, 1)
			video_cnt = video_cnt + 1
			video_Tb[video_cnt] = path..list_token..filename
		
		elseif cur_file_type == 'mp3'
		then
			set_enable(sc_music, 23, 1)
			music_cnt = music_cnt + 1
			music_Tb[music_cnt] = path..list_token..filename
		
		elseif cur_file_type == 'MP3'
		then
			set_enable(sc_music, 23, 1)
			music_cnt = music_cnt + 1
			music_Tb[music_cnt] = path..list_token..filename
		
		end 
	else
		
	end

end

function on_init()
	--uart_setup(115200, 0, 1, 8)
	set_button_notify_release()
	set_enable(sc_music, 23, 0)
	set_enable(sc_video, 23, 0)
	
	stop_animation(sc_music, 31)
	stop_animation(sc_music, 32)
	
	for i = 1, 5
	do
		set_visiable(sc_music, (0 + i), 0)
		set_visiable(sc_video, (0 + i), 0)
	end
	
	my_set_volume_visiable(0)
	my_set_volume_value(80)
end

function on_timer(timer_id) 
	 
	if timer_id == video_timer
	then
		set_value(sc_video, 21, 0)
		my_setvisable_video_progress(0)
		
	elseif timer_id == volume_timer
	then
		set_value(sc_video, 36, 0)
		set_value(sc_music, 36, 0)
		my_set_volume_visiable(0)
	end	
end

function on_audio_callback(state)
	
	paly_music_index = paly_music_index + 1
	if paly_music_index > music_cnt
	then
		paly_music_index = 1
	end
	
	local cur_play_page = math.modf(paly_music_index / 5)
	if paly_music_index % 5 > 0
	then
		cur_play_page = cur_play_page + 1
	end
	
	if cur_play_page == get_value(sc_music, 17)
	then
		my_set_music_playstate(paly_music_index)
	else
		my_set_music_playstate(0)
	end
	
	my_set_music_playgif()
	play_sound(music_Tb[paly_music_index])
	
end

function on_video_notify(msg,v1,v2)
	
	--print("Lua_debug : video notify msg -> "..msg.."  /  v1:v2 = "..v1..":"..v2)
	
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
		
		
		set_value(sc_video, 26, (v1/v2)*1000)
		set_text(sc_video, 27, show_v1)
		set_text(sc_video, 28, show_v2)
		
	elseif msg == 0
	then 
		paly_video_index = paly_video_index + 1
		if paly_video_index > video_cnt
		then
			paly_video_index = 1
		end
		
		local cur_play_page = math.modf(paly_video_index / 5)
		if paly_video_index % 5 > 0
		then
			cur_play_page = cur_play_page + 1
		end

		if cur_play_page == get_value(sc_video, 17)
		then
			my_set_video_playstate(paly_video_index)
		else
			my_set_video_playstate(0)
		end
		play_video(video_Tb[paly_video_index], video_x, video_y, video_w, video_h)
		my_setvisable_video_progress(1)
	end
end

function on_control_notify(screen,control,value)
	
	if screen == sc_main2
	then
		if control == 7 and value == 0
		then
			if music_cnt > 0
			then
				music_allpage = math.modf(music_cnt / 5)
				
				if music_cnt % 5 > 0
				then
					music_allpage = music_allpage + 1
				end
			end
			
			set_text(sc_music, 17, music_curpage)
			set_text(sc_music, 19, music_allpage)
			my_show_cur_musicpage(music_curpage)
			
			change_screen(sc_music)
			
		elseif control == 8 and value == 0
		then
			if video_cnt > 0
			then
				video_allpage = math.modf(video_cnt / 5)
				
				if video_cnt % 5 > 0
				then
					video_allpage = video_allpage + 1
				end
			end

			set_text(sc_video, 17, video_curpage)
			set_text(sc_video, 19, video_allpage)
			my_show_cur_videopage(video_curpage)
			my_setvisable_video_progress(0)
			change_screen(sc_video)
		end
	end
	
	if screen == sc_music
	then
		
		if control == 16 and value == 0 then
			
			local cur_page = 0
			
			if music_cnt > 0
			then
				cur_page = get_value(sc_music, 17)
				
				cur_page = cur_page - 1
				if cur_page <= 1
				then
					cur_page = 1
				end
				set_value(sc_music, 17, cur_page)
				my_show_cur_musicpage(cur_page) 
			end

		elseif control == 20 and value == 0
		then	
			
			local cur_page = 0
			local all_page = 0
			
			if music_cnt > 0
			then
				cur_page = get_value(sc_music, 17)
				all_page = get_value(sc_music, 19)
				
				cur_page = cur_page + 1
				if cur_page > all_page
				then
					cur_page = all_page
				end
				
				set_value(sc_music, 17, cur_page)
				my_show_cur_musicpage(cur_page) 
			end	
			
		elseif (control >= 11 and control <= 15) and value == 0
		then 
			set_value(sc_music, 23, 1)
			set_value(sc_video, 23, 0)
			
			stop_video()
			local cur_page = 0
			if music_cnt > 0
			then
				cur_page = get_value(sc_music, 17)
				paly_music_index = (cur_page - 1) * 5 + (control - 10)
				my_set_music_playstate(paly_music_index)
				
				if paly_music_index <= music_cnt
				then
					stop_sound()
					play_sound(music_Tb[paly_music_index])
					my_set_music_playgif()
				end
				
			end 
			
		elseif (control >= 22 and control <= 24) and music_cnt > 0
		then
			stop_video()
			if control ~= 23
			then
				set_value(sc_music, 23, 1)
				set_value(sc_video, 23, 0)
				
				if control == 22 and value == 0
				then
					paly_music_index = paly_music_index - 1
					
					if paly_music_index < 1
					then
						paly_music_index = music_cnt
					end
					
				elseif control == 24 and value == 0
				then
					paly_music_index = paly_music_index + 1
					if paly_music_index > music_cnt
					then
						paly_music_index = 1
					end
				end
				
				local cur_play_page = math.modf(paly_music_index / 5)
				if paly_music_index % 5 > 0
				then
					cur_play_page = cur_play_page + 1
				end
				
				set_value(sc_music, 17, cur_play_page)
				
				my_show_cur_musicpage(cur_play_page)
				my_set_music_playstate(paly_music_index)
				
				play_sound(music_Tb[paly_music_index])
				my_set_music_playgif()
				
			else
				if value == 0
				then
					stop_video()
					stop_sound()
					stop_animation(sc_music, 31)
					stop_animation(sc_music, 32)
					
				elseif value == 1
				then
					stop_video()
					play_sound(music_Tb[paly_music_index])
					my_set_music_playgif()
				end
			end
			
		elseif control == 25
		then
			
		elseif control == 31
		then
			stop_animation(sc_music, 31)
			stop_animation(sc_music, 32)
			
			play_animation(sc_music, 32)
			
		elseif control == 36 and value == 0
		then
			my_set_volume_visiable(0)
			
		elseif control == 36 and value == 1
		then
			my_set_volume_visiable(1)
			
		elseif control == 40 
		then
			my_set_volume_visiable(1)
			set_value(sc_video, 39, value)
			set_value(sc_video, 40, value)
			my_set_volume_value(math.modf(value))
		
		end
	end
	
	if screen == sc_video
	then
		if control == 16 then
			
			local cur_page = 0

			if video_cnt > 0
			then
				cur_page = get_value(sc_video, 17)

				cur_page = cur_page - 1
				if cur_page <= 1
				then
					cur_page = 1
				end
				
				set_value(sc_video, 17, cur_page)
				my_show_cur_videopage(cur_page) 
			end

		elseif control == 20 
		then	
			local cur_page = 0
			local all_page = 0
			
			if video_cnt > 0
			then
				cur_page = get_value(sc_video, 17)
				all_page = get_value(sc_video, 19)
				
				cur_page = cur_page + 1
				if cur_page > all_page
				then
					cur_page = all_page
				end
				
				set_value(sc_video, 17, cur_page)
				my_show_cur_videopage(cur_page) 
			end

		elseif control == 21 and value == 0
		then
			my_setvisable_video_progress(0)
		elseif control == 21 and value == 1
		then
			my_setvisable_video_progress(1)
		
		elseif (control >= 11 and control <= 15) and value == 0
		then 
			set_value(sc_video, 23, 1)
			
			set_value(sc_music, 23, 0)

			stop_animation(sc_music, 31)
			stop_animation(sc_music, 32)
			
			stop_sound()
			local cur_page = 0
			if video_cnt > 0
			then
				cur_page = get_value(sc_video, 17)
				paly_video_index = (cur_page - 1) * 5 + (control - 10)
				my_set_video_playstate(paly_video_index)
				
				if paly_video_index <= video_cnt
				then	
					play_video(video_Tb[paly_video_index], video_x, video_y, video_w, video_h)
					my_setvisable_video_progress(1)
				end
				
			end 
			
		elseif (control >= 22 and control <= 24) and video_cnt > 0
		then
			stop_sound()
			
			if control ~= 23
			then
				set_value(sc_video, 23, 1)
				
				set_value(sc_music, 23, 0)

				stop_animation(sc_music, 31)
				stop_animation(sc_music, 32)
				
				if control == 22 and value == 0
				then
					paly_video_index = paly_video_index - 1
					
					if paly_video_index < 1
					then
						paly_video_index = video_cnt
					end
					
				elseif control == 24 and value == 0
				then
					paly_video_index = paly_video_index + 1
					if paly_video_index > video_cnt
					then
						paly_video_index = 1
					end
				end
				
				local cur_play_page = math.modf(paly_video_index / 5)
				if paly_video_index % 5 > 0
				then
					cur_play_page = cur_play_page + 1
				end
				
				set_value(sc_video, 17, cur_play_page)
				
				my_show_cur_videopage(cur_play_page)
				my_set_video_playstate(paly_video_index)
				
				play_video(video_Tb[paly_video_index], video_x, video_y, video_w, video_h)
				my_setvisable_video_progress(1)
				
			else
				if value == 0
				then
					stop_sound()
					pause_video()
					
				elseif value == 1
				then
					stop_sound()
					
					set_value(sc_music, 23, 0)
					stop_animation(sc_music, 31)
					stop_animation(sc_music, 32)
					
					resume_video()
					my_setvisable_video_progress(1)
				end	
			end
			
		elseif control == 36 and value == 0
		then
			my_set_volume_visiable(0)
		
		elseif control == 36 and value == 1
		then
			my_set_volume_visiable(1)
			
		elseif control == 40 
		then
			my_set_volume_visiable(1)
			
			set_value(sc_music, 39, value)
			set_value(sc_music, 40, value)
			my_set_volume_value(math.modf(value))
		end
	end
end


function on_power_on()
	
	sd_dir = '3:/'
	list_dir(sd_dir)
	
	if cur_screen == sc_music
	then
		if music_cnt > 0
		then

			music_allpage = math.modf(music_cnt / 5)
			
			if music_cnt % 5 > 0
			then
				music_allpage = music_allpage + 1
			end
			
			set_text(sc_music, 17, music_curpage)
			set_text(sc_music, 19, music_allpage)
			my_show_cur_musicpage(music_curpage)
		end
	
	elseif cur_screen == sc_video
	then

		if video_cnt > 0
		then
			
			video_allpage = math.modf(video_cnt / 5)
			
			if video_cnt % 5 > 0
			then
				video_allpage = video_allpage + 1
			end

			set_text(sc_video, 17, video_curpage)
			set_text(sc_video, 19, video_allpage)
			my_show_cur_videopage(video_curpage)
		end
	end

end

-- 系统回调函数，插入SD卡自动回调
function on_sd_inserted(dir)
	print("Lua_DebugMsg -> insert sd disk : "..dir)
	local cur_screen = get_current_screen()
	
	sd_dir = dir
	list_dir(sd_dir)
	
	if cur_screen == sc_music
	then
		if music_cnt > 0
		then

			music_allpage = math.modf(music_cnt / 5)
			
			if music_cnt % 5 > 0
			then
				music_allpage = music_allpage + 1
			end
			
			set_text(sc_music, 17, music_curpage)
			set_text(sc_music, 19, music_allpage)
			my_show_cur_musicpage(music_curpage)
		end
	
	elseif cur_screen == sc_video
	then

		if video_cnt > 0
		then
			
			video_allpage = math.modf(video_cnt / 5)
			
			if video_cnt % 5 > 0
			then
				video_allpage = video_allpage + 1
			end

			set_text(sc_video, 17, video_curpage)
			set_text(sc_video, 19, video_allpage)
			my_show_cur_videopage(video_curpage)
		end
	end
end

-- 系统回调函数，拔掉SD卡自动回调
function on_sd_removed()
	
	print("Lua_DebugMsg -> remove sd disk")
	my_setvisable_video_progress(0)
	
	music_Tb      = {}
	music_cnt     = 0
	music_curpage = 1
	music_allpage = 1
	paly_music_index = 0
	
	set_value(sc_music, 17, music_curpage)
	set_value(sc_music, 19, music_allpage)
	set_value(sc_music, 23, 0)
	set_enable(sc_music, 23, 0)
	
	stop_sound()
	stop_animation(sc_music, 31)
	stop_animation(sc_music, 32)
	
	for i = 1, 5 
	do	
		set_text(sc_music, (5 + i), ' ')
		set_visiable(sc_music, (0 + i), 0)
		set_value(sc_music, (0 + i), 0)    
		set_enable(sc_music, (10 + i), 0)  
		set_fore_color(sc_music, i + 5, noplay_color)
		set_text_roll(sc_music, i + 5, 0)   
	end

	video_Tb      = {}
	video_cnt     = 0
	video_curpage = 1
	video_allpage = 1
	paly_video_index = 0

	set_value(sc_video, 17, video_curpage)
	set_value(sc_video, 19, video_allpage)
	set_value(sc_video, 23, 0)
	set_enable(sc_video, 23, 0)
	stop_video()
	
	for i = 1, 5 
	do
		set_text(sc_video, (5 + i), ' ')
		set_visiable(sc_video, (0 + i), 0)
		set_value(sc_video, (0 + i), 0)   
		set_enable(sc_video, (10 + i), 0)    
		set_fore_color(sc_video, i + 5, noplay_color)
		set_text_roll(sc_video, i + 5, 0)   
	end
	
end