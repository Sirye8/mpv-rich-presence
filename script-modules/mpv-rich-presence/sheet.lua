-- SPDX-License-Identifier: AGPL-3.0-or-later
local assdraw = require("mp.assdraw")
local ok_input, input = pcall(require, "mp.input")
local theme = {surface="21201F", raised="302E2C", text="F6F2EF", muted="CDC7C2",
    primary="E5CC9F", focus="584B38", line="59534E", hint="413B34"}
local row_step, row_height = 32, 29
local mouse_section = "rich-presence-panel"
local function safe(s)
    s = tostring(s or ""):gsub("[%c]", " ")
    return (s:gsub("\\", "\\\239\187\191"):gsub("{", "\\{"):gsub("}", "\\}"))
end
local function short(s, limit)
    local out, width = {}, 0
    for ch in tostring(s):gmatch("[%z\1-\127\194-\244][\128-\191]*") do
        width = width + (#ch > 2 and 2 or 1)
        if width > limit then return table.concat(out) .. "…" end
        out[#out+1] = ch
    end
    return table.concat(out)
end
return function(model)
    local ui = {open=false, page="Menu", memory={}, stack={}, hits={}}
    local overlay, timer, keys, editing, down = nil, nil, {}, false, nil
    local last_frame
    local toast_timer
    local hover_timer, animation_timer, hovered, tooltip
    local panel_alpha, tooltip_alpha = 1, 1
    local scale = 1
    local function dimensions()
        local w,h = mp.get_osd_size()
        if not w or w<=0 or not h or h<=0 then return end
        local factor = math.max(0.38, math.min(h/820, w/455, 1.4)) * (model.scale or 1)
        factor = math.min(factor, w/300, h/380)
        return w/factor,h/factor,factor
    end
    local function columns(w,h)
        local wide = w>=750 and h>=570 and ui.page~="Menu"
        local left = 12
        local right = math.min(w-12, left+(wide and 650 or 438))
        return left,right,left+(wide and 190 or 0),wide
    end
    function ui.text_columns(size)
        local w,h = dimensions()
        if not w then return 72 end
        local _,right,pane = columns(w,h)
        return math.max(3,math.floor((right-pane-32)/((size or 15)*0.58)))
    end
    local function clear_hover()
        if hover_timer then hover_timer:kill(); hover_timer=nil end
        hovered,tooltip=nil,nil
        tooltip_alpha=1
    end
    local function fade(kind)
        if animation_timer then animation_timer:kill(); animation_timer=nil end
        panel_alpha,tooltip_alpha=1,1
        if model.animate==false then return end
        local duration = kind=="panel" and 0.14 or 0.11
        local start = mp.get_time()
        if kind=="panel" then panel_alpha=.7 else tooltip_alpha=.4 end
        animation_timer=mp.add_periodic_timer(1/30,function()
            local t=math.min(1,(mp.get_time()-start)/duration)
            local eased=1-(1-t)^3
            if kind=="panel" then panel_alpha=.7+.3*eased else tooltip_alpha=.4+.6*eased end
            if t==1 then animation_timer:kill(); animation_timer=nil end
            ui.draw()
        end)
    end
    local function unbind()
        for _, id in ipairs(keys) do mp.remove_key_binding(id) end
        keys = {}
        mp.disable_key_bindings(mouse_section)
    end
    local function bind(key, fn, opts)
        local id = "rich_presence_sheet_" .. key
        mp.add_forced_key_binding(key, id, fn, opts)
        keys[#keys+1] = id
    end
    function ui.close()
        ui.open = false
        clear_hover()
        if animation_timer then animation_timer:kill(); animation_timer=nil end
        if toast_timer then toast_timer:kill(); toast_timer=nil end
        if timer then timer:kill(); timer=nil end
        if overlay then overlay:remove(); overlay=nil end
        last_frame=nil
        mp.unobserve_property(ui.draw)
        mp.unobserve_property(ui.mouse_moved)
        unbind()
        ui.stack, ui.hits, down = {}, {}, nil
        model.closed()
    end
    function ui.push(page)
        ui.stack[#ui.stack+1] = ui.page
        ui.page = page
        clear_hover(); fade("panel")
        ui.draw()
    end
    function ui.back()
        if model.leaving then model.leaving(ui.page) end
        ui.page = table.remove(ui.stack)
        if ui.page then clear_hover(); fade("panel"); ui.draw() else ui.close() end
    end
    function ui.draw()
        if not ui.open or editing then return end
        local rows = model.rows(ui.page)
        local mem = ui.memory[ui.page] or {focus=1, top=1}
        ui.memory[ui.page] = mem
        if mem.id then
            for i,row in ipairs(rows) do if row.id==mem.id then mem.focus=i; break end end
        end
        mem.focus = math.max(1,math.min(#rows,mem.focus))
        mem.id=rows[mem.focus] and rows[mem.focus].id
        local w,h,factor = dimensions()
        if not w then
            local lines={ui.page}
            for i,row in ipairs(rows) do lines[#lines+1] = (i==mem.focus and "> " or "  ") .. row.label end
            mp.set_property("term-status-msg",table.concat(lines,"\n"))
            ui.terminal = true
            return
        end
        scale = factor
        overlay.res_x,overlay.res_y = math.floor(w),math.floor(h)
        local left,right,pane,wide = columns(w,h)
        local top = 12
        local available = math.max(1,math.floor((h*.57-top-88)/row_step))
        local shown = math.max(1,math.min(#rows,available))
        local bottom = top+88+row_step*math.max(shown,wide and #model.sections or 0)
        mp.set_mouse_area(math.floor(left*scale),math.floor(top*scale),
            math.ceil(right*scale),math.ceil(bottom*scale),mouse_section)
        local ass = assdraw.ass_new()
        local opacity = panel_alpha
        local function alpha() return string.format("\\alpha&H%02X&",math.floor(255*(1-opacity))) end
        local function box(x,y,r,b,color,radius)
            ass:new_event(); ass:pos(0,0)
            ass:append("{\\bord0\\shad0\\1c&H"..color.."&"..alpha().."}")
            ass:draw_start(); ass:round_rect_cw(x,y,r,b,radius or 8); ass:draw_stop()
        end
        local function label(x,y,text,size,color,space)
            ass:new_event(); ass:pos(x,y); ass:an(7)
            ass:append("{\\fnSegoe UI\\fs"..size.."\\bord0\\shad0\\q2\\1c&H"..color.."&"..alpha().."}"..
                safe(short(text,math.max(3,math.floor(space/(size*0.58))))))
        end
        ui.hits = {}
        local function hit(x,y,r,b,id,fn,title,tip)
            ui.hits[#ui.hits+1]={x=x,y=y,r=r,b=b,id=id,fn=fn,title=title,tip=tip}
        end
        box(left,top,right,bottom,theme.surface,16)
        if wide then
            label(left+13,top+12,"Rich Presence",17,theme.primary,164)
            for i,page in ipairs(model.sections) do
                local y=top+44+(i-1)*row_step
                if page==ui.page or hovered=="nav"..i then box(left+6,y,pane-6,y+row_height,theme.focus) end
                label(left+13,y+6,page,14,theme.text,pane-left-21)
                hit(left+6,y,pane-6,y+row_height,"nav"..i,function()
                    if model.leaving then model.leaving(ui.page) end
                    ui.stack={"Menu"}; ui.page=page
                    clear_hover(); fade("panel"); model.refresh(); ui.draw()
                end,page,model.help and model.help(page))
            end
        end
        label(pane+13,top+11,ui.page,19,theme.text,right-pane-26)
        mem.top=math.max(1,math.min(mem.top,mem.focus))
        if mem.focus>=mem.top+available then mem.top=mem.focus-available+1 end
        for i=mem.top,math.min(#rows,mem.top+available-1) do
            local row=rows[i]
            local y=top+44+(i-mem.top)*row_step
            local color=(mem.focus==i or hovered==i) and theme.focus or theme.raised
            if down==i or row.primary then color=theme.primary end
            box(pane+6,y,right-6,y+row_height,color)
            label(pane+13,y+6,row.label,15,(row.primary or down==i) and theme.surface or
                (row.disabled and theme.muted or theme.text),right-pane-(row.toggle~=nil and 78 or 32))
            if row.toggle~=nil then
                local sx=right-52
                box(sx,y+5,sx+39,y+24,row.toggle and theme.primary or theme.line,10)
                local knob=sx+(row.toggle and 22 or 3)
                box(knob,y+8,knob+14,y+21,row.toggle and theme.surface or theme.muted,7)
            end
            hit(pane+6,y,right-6,y+row_height,i,not row.disabled and row.run or nil,row.label,row.tip)
        end
        if ui.page=="Appearance" then
            local mid=(pane+right)/2
            box(pane+6,bottom-36,mid-3,bottom-6,theme.raised)
            label(pane+13,bottom-30,"Cancel",15,theme.text,mid-pane-24)
            hit(pane+6,bottom-36,mid-3,bottom-6,"cancel",ui.back,"Cancel","Discard the unsaved preview.")
            box(mid+3,bottom-36,right-6,bottom-6,theme.primary)
            label(mid+13,bottom-30,"Apply",15,theme.surface,right-mid-24)
            for _,r in ipairs(rows) do
                if r.label=="Apply" then hit(mid+3,bottom-36,right-6,bottom-6,"apply",r.run,"Apply",r.tip) end
            end
        else
            local title=ui.page=="Menu" and "Close" or "Back"
            box(pane+6,bottom-36,right-6,bottom-6,hovered=="back" and theme.focus or theme.raised)
            label(pane+13,bottom-30,title,15,theme.primary,right-pane-32)
            hit(pane+6,bottom-36,right-6,bottom-6,"back",ui.back,title,
                ui.page=="Menu" and "Close the panel." or "Return to the previous page.")
        end
        if #rows>shown then
            local y=top+44
            local size=row_step*shown-3
            local first=y+size*(mem.top-1)/#rows
            box(right-4,first,right-2,first+size*shown/#rows,theme.primary,1)
        end
        if tooltip then
            for _,target in ipairs(ui.hits) do
                if target.id==tooltip and target.tip then
                    local width=math.min(300,w-24)
                    local side=w-right>=width+20
                    local x=side and right+8 or left
                    local lines,line={}, ""
                    local limit=math.floor((width-24)/(14*.58))
                    for word in target.tip:gmatch("%S+") do
                        if #line>0 and #line+#word+1>limit then lines[#lines+1]=line; line="" end
                        line=line=="" and word or line.." "..word
                    end
                    if line~="" then lines[#lines+1]=line end
                    local height=38+18*#lines
                    local y=math.max(top,math.min(side and target.y or bottom+8,h*.79-height))
                    opacity=tooltip_alpha
                    box(x,y,x+width,y+height,theme.hint,10)
                    label(x+12,y+8,target.title,15,theme.primary,width-24)
                    for i,value in ipairs(lines) do label(x+12,y+14+i*18,value,14,theme.text,width-24) end
                    break
                end
            end
        end
        local frame=tostring(overlay.res_x)..":"..tostring(overlay.res_y)..ass.text
        if frame~=last_frame then overlay.data=ass.text; overlay:update(); last_frame=frame end
    end
    local function focus(delta)
        local m=ui.memory[ui.page]
        clear_hover()
        m.id=nil
        m.focus=m.focus+delta
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
        for _,r in ipairs(ui.hits) do
            if x>=r.x and x<r.r and y>=r.y and y<r.b then
                target=r; break
            end
        end
        local id=target and target.id
        if event=="hover" then
            if id==hovered then return end
            clear_hover()
            hovered=id
            if target and target.tip then
                hover_timer=mp.add_timeout(.38,function()
                    hover_timer=nil; tooltip=id; fade("tooltip"); ui.draw()
                end)
            end
        elseif event=="down" then
            down=id
            if type(id)=="number" then ui.memory[ui.page].focus=id; ui.memory[ui.page].id=nil end
        elseif event=="up" then
            local pressed=down; down=nil
            if target and pressed==id and target.fn then clear_hover(); target.fn() end
        end
        ui.draw()
    end
    ui.mouse_moved=function(_,position)
        if position and position.hover==false then clear_hover(); ui.draw()
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
        editing=true; clear_hover(); unbind(); overlay:remove(); last_frame=nil
        input.get({prompt=prompt,default_text=value or "",submit=function(s) done(s) end,
            closed=function() editing=false; if ui.open then controls(); ui.draw() end end})
    end
    function ui.toast(message)
        if ui.open then model.notice(message); return end
        if toast_timer then toast_timer:kill() end
        local w,h=mp.get_osd_size()
        if not w or w<=0 or h<=0 then mp.msg.info(message); return end
        overlay=overlay or mp.create_osd_overlay("ass-events")
        overlay.res_y=720; overlay.res_x=math.floor(w*720/h)
        local ass=assdraw.ass_new()
        local right=overlay.res_x-16
        local left=math.max(8,right-460)
        ass:pos(0,0); ass:append("{\\bord0\\shad0\\1c&H"..theme.raised.."&}")
        ass:draw_start(); ass:round_rect_cw(left,644,right,704,12); ass:draw_stop()
        ass:new_event(); ass:pos(left+16,663); ass:an(7)
        ass:append("{\\fs18\\bord0\\1c&H"..theme.text.."&}"..safe(short(message,(right-left-32)/11)))
        overlay.data=ass.text; overlay:update()
        toast_timer=mp.add_timeout(4,function()
            if not ui.open and overlay then overlay:remove(); overlay=nil end
            toast_timer=nil
        end)
    end
    function ui.show(page)
        if ui.open then
            if page then ui.page=page; ui.stack={"Menu"}; clear_hover(); fade("panel"); model.refresh(); ui.draw()
            else ui.close() end
            return
        end
        if toast_timer then toast_timer:kill(); toast_timer=nil end
        overlay=overlay or mp.create_osd_overlay("ass-events")
        ui.old_term=mp.get_property("term-status-msg",""); ui.terminal=false
        ui.open=true; ui.page=page or "Menu"; ui.stack=page and {"Menu"} or {}
        controls()
        fade("panel")
        mp.observe_property("osd-dimensions","native",ui.draw)
        mp.observe_property("mouse-pos","native",ui.mouse_moved)
        model.refresh()
        timer=mp.add_periodic_timer(1,model.refresh)
        ui.draw()
    end
    return ui
end
