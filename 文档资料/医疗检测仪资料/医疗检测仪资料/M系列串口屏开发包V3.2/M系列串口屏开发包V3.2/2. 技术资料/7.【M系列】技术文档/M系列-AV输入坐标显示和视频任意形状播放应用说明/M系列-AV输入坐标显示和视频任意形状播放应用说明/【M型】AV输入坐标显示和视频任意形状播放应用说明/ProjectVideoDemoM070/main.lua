--下面列出了常用的回调函数
--更多功能请阅读<<物联型LUA脚本API.pdf>>

--初始化函数
--function on_init()
--end

--定时回调函数，系统每隔1秒钟自动调用。
--function on_systick()
--end

--用户通过触摸修改控件后，执行此回调函数。
--点击按钮控件，修改文本控件、修改滑动条都会触发此事件。
--function on_control_notify(screen,control,value)
--end

--当画面切换时，执行此回调函数，screen为目标画面。
--function on_screen_change(screen)
--end


--[[ 十字光标坐标显示点 ]]

local LineH_Y = 263                                                         --十字显示光标点y坐标
local LineV_X = 240                                                         --十字显示光标点x坐标

local TextH_X  = LineV_X + 5                                                --坐标text显示的坐标位置
local TextH_Y  = LineH_Y - 40 

local TextV_X  = LineV_X + 5
local TextV_Y  = LineH_Y - 20
function on_init()                                                          --初始化
    set_color_key(0x00BFBFBF,0x00C8C8C8,0x0000002A)                         --设置颜色过滤属性（必须在on_init()中配置）
end

function  on_draw(screen)                                                   --界面刷新回调函数，具体使用说明参考《LUA脚本API》
    if screen == 1 then
        set_pen_color(0x3666)                                               --设置画笔颜色   
        draw_line(40,LineH_Y,440,LineH_Y,1)                                 --画十字线    
        draw_line(LineV_X,63,LineV_X,440,1)
        local text_h = "LineH_Y:"..LineH_Y
        local text_v = "LineV_X:"..LineV_X
        draw_text(text_h,TextH_X,TextH_Y,90,20,4,0x3666,4)                  --画坐标text显示
        draw_text(text_v,TextV_X,TextV_Y,90,20,4,0x3666,4)
    end  
end


function on_control_notify(screen,control,value)                            --触摸控件回调函数
    if screen == 1 then
        if control == 6 then                                                --上
            if value == 1 then
                LineH_Y = LineH_Y - 1
                if LineH_Y < 63 then
                    LineH_Y = 63
                end
            elseif value == 2 then
                LineH_Y = LineH_Y - 5
                if LineH_Y < 63 then
                    LineH_Y = 63
                end
            end
        elseif control == 7 then                                             --下
            if value == 1 then
                LineV_X = LineV_X - 1
                if LineV_X < 63 then
                    LineV_X = 63
                end
            elseif value == 2 then
                LineV_X = LineV_X - 5
                if LineV_X < 63 then
                    LineV_X = 63
                end
            end
        elseif control == 8 then                                             --左
            if value == 1 then
                LineH_Y = LineH_Y + 1
                if LineH_Y > 440 then
                    LineH_Y = 463
                end
            elseif value == 2 then
                LineH_Y = LineH_Y + 5
                if LineH_Y > 440 then
                    LineH_Y = 463
                end
            end
        elseif control == 9 then                                             --右
            if value == 1 then
                LineV_X = LineV_X + 1
                if LineV_X > 440 then
                    LineV_X = 440
                end
            elseif value == 2 then
                LineV_X = LineV_X + 5
                if LineV_X > 440 then
                    LineV_X = 440
                end
            end
        end
        follow_pos(LineV_X,LineH_Y)                                          --text显示坐标传入
        redraw()
    end
end


function on_press(state,x,y)                                   --触摸屏点击回调函数                                      
    if get_current_screen() == 1 then
        if state == 1 or state == 2 then
            if x >= 40 and x <= 440 then
                if y >= 63 and y <= 463 then
                    follow_pos(x,y)
                    LineV_X = x
                    LineH_Y = y
                    redraw()
                end
            end
        end 
    end
end


function follow_pos(p_x,p_y)                     --text坐标显示
    print("p_x = "..p_x)
    print("p_y = "..p_y)
    if p_x < 240 and p_y < 263 then
        TextV_X = p_x + 5
        TextH_X = p_x + 5
        TextV_Y = p_y + 5
        TextH_Y = p_y + 25
    elseif p_x < 240 and p_y > 263 then
        TextV_X = p_x + 5
        TextH_X = p_x + 5
        TextV_Y = p_y - 40
        TextH_Y = p_y - 20
    elseif p_x >= 240 and p_y <= 263 then
        TextV_X = p_x - 85
        TextH_X = p_x - 85
        TextV_Y = p_y + 5
        TextH_Y = p_y + 25
    elseif p_x > 240 and p_y > 263 then
        TextV_X = p_x - 85
        TextH_X = p_x - 85
        TextV_Y = p_y - 40
        TextH_Y = p_y - 20
    end
end