-- SPDX-License-Identifier: AGPL-3.0-or-later
local assdraw = require("mp.assdraw")
local ok_input, input = pcall(require, "mp.input")
-- ASS colors are BGR. The video output composites the alpha under the whole sheet.
local theme = {black="0B0908", rail="080605", row="201C19", hover="40352B", focus="594636",
    text="F7F3EF", muted="C2B8AD", accent="FFD9AF", ink="1B1008", rim="ABA096",
    switch="5F5348", hint="18130F"}
local row_step, row_height = 32, 29
local mouse_section = "rich-presence-panel"

local function short(text, limit)
    local out, width = {}, 0
    for ch in text:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
        width = width + (#ch > 2 and 2 or 1)
        if width > limit then return table.concat(out) .. "…" end
        out[#out+1] = ch
    end
    return text
end

local function fit(text, size, space)
    text = short(tostring(text or ""):gsub("[%c]", " "), math.max(3, math.floor(space/(size*.58))))
    return (text:gsub("\\", "\\\239\187\191"):gsub("{", "\\{"):gsub("}", "\\}"))
end

local function alpha(opacity)
    return string.format("%02X", math.floor(255*(1-opacity)+.5))
end

local function box(ass, x,y,r,b, color, opacity, radius, rim)
    ass:new_event(); ass:an(7); ass:pos(0,0)
    ass:append("{\\shad0\\bord"..(rim and 1 or 0).."\\1c&H"..color.."&\\1a&H"..alpha(opacity).."&"..
        (rim and "\\3c&H"..theme.rim.."&\\3a&H"..alpha(rim).."&" or "").."}")
    ass:draw_start(); ass:round_rect_cw(x,y,r,b,radius or 8); ass:draw_stop()
end

local function label(ass, x,y, text, size, color, opacity, align, bold)
    ass:new_event(); ass:an(align or 7); ass:pos(x,y)
    ass:append("{\\fnSegoe UI\\fs"..size.."\\b"..(bold and 1 or 0).."\\bord0\\shad0\\q2\\1c&H"..color..
        "&\\alpha&H"..alpha(opacity).."&}"..text)
end

return function(model)
    local ui = {open=false, page="Menu", memory={}, stack={}, hits={}}
    local overlay, layout, refresh_timer, toast_timer, hover_timer, animation_timer
    local last_frame, last_terminal
    local keys, editing, pressed, hovered, tooltip = {}
    local scale = 1
    local motion = {shell=0, page=1, focus=1, hover=0, hint=0}
    local tweens = {}

    local function dimensions()
        local w,h = mp.get_osd_size()
        if not w or w<=0 or not h or h<=0 then return end
        local factor = math.max(.76, math.min(h/740, 2.4)) * (model.scale or 1)
        factor = math.min(factor, w/300, h/380)
        return w/factor,h/factor,factor
    end

    local function columns(w,h)
        local margins = mp.get_property_native("user-data/osc/margins",{})
        local min_left,max_right = 12+w*(margins.l or 0),w-12-w*(margins.r or 0)
        local min_top = 12+h*(margins.t or 0)
        if mp.get_property_native("border") == false and mp.get_property_native("osc") ~= false then
            min_top=math.max(44,min_top)
        end
        local max_bottom = math.min(h*.86,h-12-h*(margins.b or 0))
        local span = max_right-min_left
        local wide = span>=716 and h>=560 and max_bottom-min_top>=88+row_step*#model.sections and ui.page~="Menu"
        local width = math.min(span,wide and 670 or 438)
        local position = model.position and model.position() or "top-left"
        local left = min_left
        if position:match("%-right$") then left=max_right-width
        elseif position:match("%-center$") then left=(min_left+max_right-width)/2 end
        return left,left+width,left+(wide and 220 or 0),wide,position,min_top,max_bottom
    end

    function ui.text_columns(size)
        local w,h = dimensions()
        if not w then return 72 end
        local _,right,pane = columns(w,h)
        return math.max(3,math.floor((right-pane-32)/((size or 15)*.58)))
    end

    local function remove_overlay()
        if overlay then overlay:remove(); overlay=nil end
        last_frame=nil
    end

    local function paint(ass, w,h)
        local frame = w..":"..h..ass.text
        if frame==last_frame then return end
        overlay = overlay or mp.create_osd_overlay("ass-events")
        overlay.res_x,overlay.res_y,overlay.data = w,h,ass.text
        overlay:update()
        last_frame=frame
    end

    local function render()
        if editing or not layout or motion.shell<=0 then return end
        local ass = assdraw.ass_new()
        local left,top,right,bottom,pane = layout.left,layout.top,layout.right,layout.bottom,layout.pane
        local shell = motion.shell
        local content = shell*(.58+.42*motion.page)
        local offset = layout.direction*math.floor(6*(1-motion.page)+.5)
        local rise = math.floor(4*(1-shell)+.5)
        box(ass,left+1,top+4,right+1,bottom+5,theme.rail,.34*shell,16)
        box(ass,left,top,right,bottom,theme.black,.87*shell,16,.34*shell)
        if layout.wide then
            box(ass,left+5,top+5,pane-5,bottom-5,theme.rail,.34*shell,12)
            label(ass,left+14,top+13,"Rich Presence",16,theme.accent,shell,nil,true)
            for _,item in ipairs(layout.nav) do
                local active = item.title==layout.page
                local highlight = hovered==item.key and motion.hover or 0
                if active or highlight>0 then
                    box(ass,item.x,item.y,item.r,item.b,theme.focus,(active and .82 or .58*highlight)*shell,8)
                end
                if active then
                    box(ass,item.x+4,item.y+7,item.x+7,item.b-7,theme.accent,.92*shell,1.5)
                end
                label(ass,item.x+13,item.y+5,item.display,15,active and theme.text or theme.muted,shell)
            end
        end
        label(ass,pane+14+offset,top+10+rise,layout.title,20,theme.text,content,nil,true)
        box(ass,pane+9,top+39,right-9,top+40,theme.rim,.22*shell,.5)
        for _,item in ipairs(layout.rows) do
            local is_pressed = pressed==item.key and not item.disabled
            local highlight = hovered==item.key and motion.hover or 0
            local focus = item.focused and motion.focus or 0
            local inset = is_pressed and 1 or 0
            local x,y,r,b = item.x+inset,item.y+inset,item.r-inset,item.b-inset
            if item.plain then
                label(ass,x+5+offset,y+5+rise,item.display,15,theme.text,content)
            elseif item.primary or is_pressed then
                box(ass,x,y,r,b,theme.accent,(is_pressed and .81 or .96)*content)
            else
                box(ass,x,y,r,b,theme.row,(item.disabled and .35 or .61)*content)
                local emphasis = math.max(.76*focus,.52*highlight)
                if emphasis>0 then box(ass,x,y,r,b,theme.focus,emphasis*content) end
            end
            if not item.plain and item.focused and not item.primary and not is_pressed then
                box(ass,x+4,y+7,x+7,b-7,theme.accent,focus*content,1.5)
            end
            local color = (item.primary or is_pressed) and theme.ink or (item.disabled and theme.muted or theme.text)
            if not item.plain then label(ass,x+13+offset,y+5+rise,item.display,15,color,content,nil,item.primary) end
            if item.value then
                local color = item.primary and theme.ink or (item.disabled and theme.muted or theme.accent)
                label(ass,r-(item.adjust and 26 or 11),y+6+rise,item.value,14,color,content,9)
            end
            if item.fn and item.toggle==nil and (item.adjust or not item.value) and not item.primary then
                label(ass,r-10,y+4+rise,"›",16,theme.accent,.8*content,9)
            end
            if item.toggle~=nil then
                local sx = r-46
                local knob = sx+(item.toggle and 20 or 3)
                box(ass,sx,y+6,sx+36,y+23,item.toggle and theme.accent or theme.switch,content,8)
                box(ass,knob,y+8,knob+13,y+21,item.toggle and theme.ink or theme.text,content,6)
            end
        end
        for _,item in ipairs(layout.footer) do
            local emphasis = hovered==item.key and motion.hover or 0
            local inset = pressed==item.key and 1 or 0
            box(ass,item.x+inset,item.y+inset,item.r-inset,item.b-inset,
                item.primary and theme.accent or theme.hover,(item.primary and .95 or .38+.48*emphasis)*shell)
            label(ass,item.x+12,item.y+5+inset,item.title,14,item.primary and theme.ink or theme.accent,shell,nil,true)
        end
        if layout.scroll then
            local scroll = layout.scroll
            box(ass,right-5,top+45,right-3,bottom-43,theme.rim,.19*shell,1)
            box(ass,right-5,scroll.first,right-3,scroll.last,theme.accent,.8*shell,1)
        end
        if tooltip and motion.hint>0 then
            local tip = tooltip
            local opacity = shell*motion.hint
            local y = tip.y + math.floor(4*(1-motion.hint)+.5)
            box(ass,tip.x,y,tip.x+tip.width,y+tip.height,theme.hint,.97*opacity,12,.45*opacity)
            label(ass,tip.x+12,y+8,tip.title,15,theme.accent,opacity,nil,true)
            for _,line in ipairs(tip.lines) do
                label(ass,tip.x+line.x,y+line.y,line.text,line.size,line.color,opacity,nil,line.bold)
            end
        end
        paint(ass,layout.w,layout.h)
    end

    local function stop_animation()
        if animation_timer then animation_timer:kill(); animation_timer=nil end
    end

    local function tick()
        local now = mp.get_time()
        for key,tween in pairs(tweens) do
            local progress = math.min(1,(now-tween.start)/tween.duration)
            motion[key] = tween.from+(tween.to-tween.from)*(1-(1-progress)^3)
            if progress==1 then
                motion[key]=tween.to
                tweens[key]=nil
            end
        end
        if motion.hint==0 then tooltip=nil end
        if not ui.open and motion.shell==0 then
            remove_overlay()
            layout=nil
        else
            render()
        end
        if not next(tweens) then stop_animation() end
    end

    local function animate(key, from, to, duration)
        if model.animate==false or from==to then
            motion[key],tweens[key] = to,nil
            return
        end
        motion[key] = from
        tweens[key] = {from=from,to=to,start=mp.get_time(),duration=duration}
        if not animation_timer then animation_timer=mp.add_periodic_timer(1/60,tick) end
    end

    local function clear_hover(immediate)
        if hover_timer then hover_timer:kill(); hover_timer=nil end
        hovered=nil
        motion.hover,tweens.hover=0,nil
        if immediate or motion.hint==0 or model.animate==false then
            motion.hint,tweens.hint,tooltip=0,nil,nil
        else
            animate("hint", motion.hint, 0, .10)
        end
    end

    local function wrap(text, width, size)
        local lines,line = {}, ""
        local limit = math.floor(width/(size*.58))
        for word in text:gmatch("%S+") do
            if #line>0 and #line+#word+1>limit then lines[#lines+1]=line; line="" end
            line=line=="" and word or line.." "..word
        end
        if line~="" then lines[#lines+1]=line end
        return lines
    end

    local function make_tip(target)
        local options = type(target.tip)=="table" and target.tip.options
        local width = math.min(options and 378 or 318,layout.w-24)
        local lines, height = {}, 30
        local function line(text,x,y,size,color,bold,space)
            lines[#lines+1]={text=fit(text,size,space),x=x,y=y,size=size,color=color,bold=bold}
        end
        if options then
            line("Options",12,height,12,theme.muted,true,width-24)
            height=height+19
            local column = math.min(122,math.floor(width*.39))
            local description_width = width-column-12
            for _,option in ipairs(options) do
                local color = option.selected and theme.accent or theme.text
                local name = option.label:sub(1,1):upper()..option.label:sub(2):gsub("-", " ")
                local marker = option.selected and "• " or ""
                line(marker..name,12,height,13,color,true,column-16)
                local detail = wrap(option.detail,description_width,13)
                for i,value in ipairs(detail) do
                    line(value,column,height+(i-1)*16,13,theme.text,false,description_width)
                end
                height=height+math.max(20,16*#detail+3)
            end
        else
            for _,value in ipairs(wrap(target.tip,width-24,14)) do
                line(value,12,height,14,theme.text,false,width-24)
                height=height+18
            end
        end
        height=height+8
        local space_right = layout.w-layout.right>=width+20
        local space_left = layout.left>=width+20
        local beside = space_right or space_left
        local x = space_right and layout.right+8 or space_left and layout.left-width-8 or
            math.min(layout.left,layout.w-width-12)
        local ceiling = math.max(layout.min_top,layout.max_bottom-height-4)
        local y = target.y
        if not beside then
            if layout.bottom+8<=ceiling then y=layout.bottom+8
            elseif layout.top-height-8>=layout.min_top then y=layout.top-height-8
            else y=target.y-height-8 end
        end
        return {x=x,y=math.max(layout.min_top,math.min(y,ceiling)),width=width,height=height,
            key=target.key,title=fit(target.title,15,width-24),lines=lines}
    end

    local function unbind()
        for _,id in ipairs(keys) do mp.remove_key_binding(id) end
        keys = {}
        mp.disable_key_bindings(mouse_section)
    end

    local function bind(key, fn, opts)
        local id = "rich_presence_sheet_" .. key
        mp.add_forced_key_binding(key,id,fn,opts)
        keys[#keys+1] = id
    end

    function ui.close()
        if not ui.open then return end
        ui.open=false
        clear_hover(true)
        if refresh_timer then refresh_timer:kill(); refresh_timer=nil end
        mp.unobserve_property(ui.draw)
        mp.unobserve_property(ui.mouse_moved)
        unbind()
        ui.stack,ui.hits,pressed,editing = {},{},nil,nil
        model.closed()
        tweens={}
        if layout and overlay then animate("shell",motion.shell,0,.14) end
        if not tweens.shell then
            stop_animation()
            remove_overlay()
            layout=nil
            motion.shell=0
        end
    end

    local function show_page(page, direction)
        if model.changed then model.changed() end
        if layout then layout.direction=direction end
        ui.page=page
        pressed=nil
        clear_hover(true)
        animate("page",0,1,.16)
        model.refresh()
        ui.draw()
    end

    function ui.push(page)
        ui.stack[#ui.stack+1] = ui.page
        show_page(page,1)
    end

    function ui.back()
        if model.leaving then model.leaving(ui.page) end
        local page = table.remove(ui.stack)
        if page then show_page(page,-1) else ui.close() end
    end

    function ui.draw()
        if not ui.open or editing then return end
        local rows = model.rows(ui.page)
        local mem = ui.memory[ui.page] or {focus=1,top=1}
        ui.memory[ui.page]=mem
        if mem.id then
            for i,row in ipairs(rows) do if row.id==mem.id then mem.focus=i; break end end
        end
        mem.focus=math.max(1,math.min(#rows,mem.focus))
        mem.id=rows[mem.focus] and rows[mem.focus].id
        local w,h,factor = dimensions()
        if not w then
            if layout then
                clear_hover(true)
                mp.set_mouse_area(0,0,0,0,mouse_section)
                ui.hits,layout={},nil
                remove_overlay()
            end
            local lines={ui.page}
            for i,row in ipairs(rows) do lines[#lines+1]=(i==mem.focus and "> " or "  ")..row.label end
            local frame=table.concat(lines,"\n")
            if frame~=last_terminal then mp.set_property("term-status-msg",frame); last_terminal=frame end
            ui.terminal=true
            return
        end
        if ui.terminal then
            mp.set_property("term-status-msg",ui.old_term)
            ui.terminal,last_terminal=false,nil
        end
        scale=factor
        local left,right,pane,wide,position,min_top,max_bottom = columns(w,h)
        local available = math.max(1,math.floor((math.min(h*.57-min_top,max_bottom-min_top)-88)/row_step))
        local shown = math.max(1,math.min(#rows,available))
        local height = 88+row_step*math.max(shown,wide and #model.sections or 0)
        local top = position:match("^bottom%-") and math.max(min_top,max_bottom-height) or min_top
        local bottom = top+height
        local bounds = {math.floor(left*scale),math.floor(top*scale),math.ceil(right*scale),math.ceil(bottom*scale)}
        local bounds_key = table.concat(bounds,":")
        if not layout or layout.bounds~=bounds_key then
            mp.set_mouse_area(bounds[1],bounds[2],bounds[3],bounds[4],mouse_section)
            clear_hover(true)
            pressed=nil
        end
        local previous = layout
        layout = {w=math.floor(w),h=math.floor(h),left=left,top=top,right=right,bottom=bottom,pane=pane,wide=wide,
            min_top=min_top,max_bottom=max_bottom,
            title=fit(ui.page=="Menu" and "Rich Presence" or ui.page,20,right-pane-36),
            page=ui.page,direction=previous and previous.direction or 1,
            bounds=bounds_key,focus=mem.id,nav={},rows={},footer={}}
        ui.hits={}
        local function hit(group,x,y,r,b,id,key,fn,title,tip)
            local item={x=x,y=y,r=r,b=b,id=id,key=key,fn=fn,title=title,tip=tip}
            group[#group+1]=item
            ui.hits[#ui.hits+1]=item
            return item
        end
        if wide then
            for i,page in ipairs(model.sections) do
                local y=top+44+(i-1)*row_step
                local key="nav"..i
                local item=hit(layout.nav,left+6,y,pane-6,y+row_height,key,key,function()
                    if model.leaving then model.leaving(ui.page) end
                    ui.stack={"Menu"}
                    show_page(page,1)
                end,page,model.help and model.help(page))
                item.display=fit(page,15,pane-left-28)
            end
        end
        mem.top=math.max(1,math.min(mem.top,mem.focus,math.max(1,#rows-available+1)))
        if mem.focus>=mem.top+available then mem.top=mem.focus-available+1 end
        for i=mem.top,math.min(#rows,mem.top+available-1) do
            local row=rows[i]
            local y=top+44+(i-mem.top)*row_step
            local key="row:"..(row.id or i)
            local item=hit(layout.rows,pane+8,y,right-8,y+row_height,i,key,not row.disabled and row.run or nil,row.label,row.tip)
            item.focused,item.disabled,item.primary,item.toggle,item.adjust,item.plain = mem.focus==i,row.disabled,row.primary,row.toggle,row.adjust,row.plain
            local title,value=row.label:match("^(.-)   (.+)$")
            local space=right-pane-48
            if row.plain then
                space=right-pane-30
            elseif row.toggle~=nil then
                space=space-43
            elseif value then
                if row.adjust then space=space-15 end
                local value_space=math.floor(math.max(space*.38,math.min(space*.72,space-#title*8.7-16)))
                item.value=fit(value,14,value_space)
                space=space-value_space-8
            elseif item.fn then
                space=space-17
            end
            item.display=fit(title or row.label,15,space)
        end
        if ui.page=="Appearance" then
            local mid=(pane+right)/2
            hit(layout.footer,pane+8,bottom-35,mid-3,bottom-7,"cancel","cancel",ui.back,"Cancel","Discard the unsaved preview.")
            for _,row in ipairs(rows) do
                if row.label=="Apply" then
                    local apply=hit(layout.footer,mid+3,bottom-35,right-8,bottom-7,"apply","apply",row.run,"Apply",row.tip)
                    apply.primary=true
                    break
                end
            end
        else
            local title=ui.page=="Menu" and "Close" or "Back"
            hit(layout.footer,pane+8,bottom-35,right-8,bottom-7,"back","back",ui.back,title,
                ui.page=="Menu" and "Close the panel." or "Return to the previous page.")
        end
        if #rows>shown then
            local size=shown*row_step-3
            local first=top+45+size*(mem.top-1)/#rows
            layout.scroll={first=first,last=first+size*shown/#rows}
        end
        if previous and previous.page==ui.page and previous.focus~=mem.id then animate("focus",.4,1,.11) end
        if tooltip then
            local updated
            for _,target in ipairs(ui.hits) do
                if target.key==tooltip.key and target.tip then updated=make_tip(target); break end
            end
            tooltip=updated
        end
        render()
    end

    local function focus(delta)
        local mem=ui.memory[ui.page]
        clear_hover(false)
        mem.id=nil
        mem.focus=mem.focus+delta
        ui.draw()
    end

    local function activate(delta)
        local row=model.rows(ui.page)[ui.memory[ui.page].focus]
        if row and not row.disabled then
            if delta and row.adjust then row.adjust(delta) elseif not delta and row.run then row.run() end
            ui.draw()
        end
    end

    local function pointer(event)
        if not ui.open or editing then return end
        local x,y=mp.get_mouse_pos()
        x,y=x/scale,y/scale
        local target
        for _,item in ipairs(ui.hits) do
            if x>=item.x and x<item.r and y>=item.y and y<item.b then target=item; break end
        end
        local key=target and target.key
        if event=="hover" then
            if key==hovered then return end
            clear_hover(false)
            hovered=key
            if target then animate("hover",.15,1,.11) end
            if target and target.tip then
                hover_timer=mp.add_timeout(.33,function()
                    hover_timer=nil
                    for _,item in ipairs(ui.hits) do
                        if item.key==hovered and item.tip then
                            tooltip=make_tip(item)
                            animate("hint",.18,1,.13)
                            render()
                            break
                        end
                    end
                end)
            end
        elseif event=="down" then
            pressed=target and target.fn and key or nil
            if pressed and type(target.id)=="number" then
                ui.memory[ui.page].focus=target.id
                ui.memory[ui.page].id=nil
                ui.draw()
                return
            end
        elseif event=="up" then
            local clicked=pressed
            pressed=nil
            if target and clicked==key and target.fn then
                clear_hover(true)
                target.fn()
                ui.draw()
                return
            end
        end
        render()
    end

    ui.mouse_moved=function(_,position)
        if position and position.hover==false then clear_hover(false); render()
        else pointer("hover") end
    end
    mp.set_key_bindings({
        {"wheel_up",function() focus(-1) end},
        {"wheel_down",function() focus(1) end},
        {"mbtn_left",nil,function() pointer("down") end,function() pointer("up") end},
        {"mbtn_left_dbl","ignore"},
    },mouse_section,"force")

    local function controls()
        bind("HOME",function() focus(-100000) end); bind("END",function() focus(100000) end)
        bind("UP",function() focus(-1) end,{repeatable=true})
        bind("DOWN",function() focus(1) end,{repeatable=true})
        bind("LEFT",function() activate(-1) end); bind("RIGHT",function() activate(1) end)
        bind("ENTER",function() activate() end); bind("ESC",ui.back)
        mp.enable_key_bindings(mouse_section,"allow-hide-cursor")
    end

    function ui.text(prompt, value, done)
        if not ok_input then model.notice("Text input unavailable. Use the script-message command."); return end
        local session={}
        editing=session
        clear_hover(true)
        pressed=nil
        tweens={}
        stop_animation()
        motion.shell,motion.page,motion.focus=1,1,1
        unbind()
        remove_overlay()
        input.get({prompt=prompt,default_text=value or "",submit=function(s)
            if editing==session then done(s) end
        end,closed=function()
            if editing~=session then return end
            editing=nil
            if ui.open then controls(); ui.draw() end
        end})
    end

    function ui.toast(message,page)
        if ui.open then model.notice(message,page); return end
        local w,h=mp.get_osd_size()
        if not w or w<=0 or h<=0 then mp.msg.info(message); return end
        if toast_timer then toast_timer:kill() end
        tweens={}
        stop_animation()
        layout=nil
        motion.shell=0
        local ass=assdraw.ass_new()
        local res=math.floor(w*720/h)
        local right=res-16
        local left=math.max(8,right-460)
        box(ass,left,644,right,704,theme.black,.92,12,.34)
        label(ass,left+16,662,fit(message,18,right-left-32),18,theme.text,1)
        paint(ass,res,720)
        toast_timer=mp.add_timeout(4,function()
            if not ui.open then remove_overlay() end
            toast_timer=nil
        end)
    end

    function ui.show(page)
        if ui.open then
            if page then ui.stack={"Menu"}; show_page(page,1) else ui.close() end
            return
        end
        if toast_timer then toast_timer:kill(); toast_timer=nil; remove_overlay() end
        ui.old_term=mp.get_property("term-status-msg","")
        ui.terminal,last_terminal=false,nil
        ui.open=true
        ui.page=page or "Menu"
        ui.stack=page and {"Menu"} or {}
        motion.page,motion.focus=1,1
        animate("shell",math.max(.20,motion.shell),1,.18)
        controls()
        mp.observe_property("osd-dimensions","native",ui.draw)
        mp.observe_property("user-data/osc/margins","native",ui.draw)
        mp.observe_property("mouse-pos","native",ui.mouse_moved)
        model.refresh()
        refresh_timer=mp.add_periodic_timer(1,model.refresh)
        ui.draw()
    end
    return ui
end
