-- SPDX-License-Identifier: AGPL-3.0-or-later
local assdraw = require("mp.assdraw")
local ok_input, input = pcall(require, "mp.input")
local theme = {surface="21201F", raised="302E2C", text="F6F2EF", muted="CDC7C2",
    primary="E5CC9F", focus="584B38", line="59534E", error="A5ADFF"}
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
    local scale = 1
    local function unbind()
        for _, id in ipairs(keys) do mp.remove_key_binding(id) end
        keys = {}
    end
    local function bind(key, fn, opts)
        local id = "rich_presence_sheet_" .. key
        mp.add_forced_key_binding(key, id, fn, opts)
        keys[#keys+1] = id
    end
    function ui.close()
        ui.open = false
        if timer then timer:kill(); timer=nil end
        if overlay then overlay:remove(); overlay=nil end
        mp.unobserve_property(ui.draw)
        unbind()
        ui.stack, ui.hits, down = {}, {}, nil
        model.closed()
    end
    function ui.push(page)
        ui.stack[#ui.stack+1] = ui.page
        ui.page = page
        ui.draw()
    end
    function ui.back()
        if model.leaving then model.leaving(ui.page) end
        ui.page = table.remove(ui.stack)
        if ui.page then ui.draw() else ui.close() end
    end
    function ui.draw()
        if not ui.open or editing then return end
        local rows = model.rows(ui.page)
        local mem = ui.memory[ui.page] or {focus=1, top=1}
        ui.memory[ui.page] = mem
        mem.focus = math.max(1,math.min(#rows,mem.focus))
        local w,h = mp.get_osd_size()
        if not w or w<=0 or h<=0 then
            local lines={ui.page}
            for i,row in ipairs(rows) do lines[#lines+1] = (i==mem.focus and "> " or "  ") .. row.label end
            mp.set_property("term-status-msg",table.concat(lines,"\n"))
            ui.terminal = true
            return
        end
        scale = math.max(0.55, math.min(h/720, w/440, 2.5)) * (model.scale or 1)
        w,h = w/scale,h/scale
        overlay.res_x,overlay.res_y = math.floor(w),math.floor(h)
        local wide = w >= 900
        local left = math.max(8,w-(wide and 824 or 512))
        local right, top, bottom = w-12,16,h-16
        local pane = left + (wide and 216 or 0)
        local ass = assdraw.ass_new()
        local function box(x,y,r,b,color,radius)
            ass:new_event(); ass:pos(0,0)
            ass:append("{\\bord0\\shad0\\1c&H"..color.."&}")
            ass:draw_start(); ass:round_rect_cw(x,y,r,b,radius or 8); ass:draw_stop()
        end
        local function label(x,y,text,size,color,space)
            ass:new_event(); ass:pos(x,y); ass:an(7)
            ass:append("{\\fnSegoe UI\\fs"..size.."\\bord0\\shad0\\q2\\1c&H"..color.."&}"..
                safe(short(text,math.max(3,math.floor(space/(size*0.58))))))
        end
        ui.hits = {}
        local function hit(x,y,r,b,id,fn)
            ui.hits[#ui.hits+1]={x=x,y=y,r=r,b=b,id=id,fn=fn}
        end
        box(left,top,right,bottom,theme.surface,16)
        if wide then
            label(left+16,top+16,"Rich Presence",20,theme.primary,185)
            for i,page in ipairs(model.sections) do
                local y=top+64+(i-1)*44
                if page==ui.page then box(left+8,y,pane-8,y+40,theme.focus) end
                label(left+16,y+10,page,16,theme.text,188)
                hit(left+8,y,pane-8,y+40,"nav"..i,function() ui.stack={"Menu"}; ui.page=page; ui.draw() end)
            end
        end
        label(pane+16,top+14,ui.page,22,theme.text,right-pane-32)
        local available=math.max(1,math.floor((bottom-top-116)/44))
        mem.top=math.max(1,math.min(mem.top,mem.focus))
        if mem.focus>=mem.top+available then mem.top=mem.focus-available+1 end
        for i=mem.top,math.min(#rows,mem.top+available-1) do
            local row=rows[i]
            local y=top+56+(i-mem.top)*44
            local color=mem.focus==i and theme.focus or theme.raised
            if down==i or row.primary then color=theme.primary end
            box(pane+8,y,right-8,y+40,color)
            label(pane+20,y+9,row.label,17,(row.primary or down==i) and theme.surface or
                (row.disabled and theme.muted or theme.text),right-pane-40)
            hit(pane+8,y,right-8,y+40,i,row.run)
        end
        box(pane+8,bottom-48,right-8,bottom-8,theme.raised)
        label(pane+20,bottom-39,ui.page=="Menu" and "Close" or "Back",16,theme.primary,right-pane-40)
        hit(pane+8,bottom-48,right-8,bottom-8,"back",ui.back)
        overlay.data=ass.text; overlay:update()
    end
    local function focus(delta)
        local m=ui.memory[ui.page]
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
        local x,y=mp.get_mouse_pos()
        x,y=x/scale,y/scale
        for _,r in ipairs(ui.hits) do
            if x>=r.x and x<r.r and y>=r.y and y<r.b then
                if type(r.id)=="number" then ui.memory[ui.page].focus=r.id end
                if event=="down" then down=r.id end
                if event=="up" and down==r.id and r.fn then down=nil; r.fn() end
                ui.draw(); break
            end
        end
        if event=="up" then down=nil end
    end
    local function controls()
        bind("UP",function() focus(-1) end,{repeatable=true})
        bind("DOWN",function() focus(1) end,{repeatable=true})
        bind("LEFT",function() activate(-1) end); bind("RIGHT",function() activate(1) end)
        bind("ENTER",function() activate() end); bind("ESC",ui.back)
        bind("WHEEL_UP",function() focus(-1) end); bind("WHEEL_DOWN",function() focus(1) end)
        bind("MOUSE_MOVE",function() pointer("hover") end)
        bind("MBTN_LEFT",function(e) pointer(e.event) end,{complex=true})
    end
    function ui.text(prompt, value, done)
        if not ok_input then model.notice("Text input unavailable. Use the script-message command."); return end
        editing=true; unbind(); overlay:remove()
        input.get({prompt=prompt,default_text=value or "",submit=function(s) done(s) end,
            closed=function() editing=false; if ui.open then controls(); ui.draw() end end})
    end
    function ui.show(page)
        if ui.open then ui.close(); return end
        overlay=mp.create_osd_overlay("ass-events")
        ui.old_term=mp.get_property("term-status-msg",""); ui.terminal=false
        ui.open=true; ui.page=page or "Menu"; ui.stack=page and {"Menu"} or {}
        controls()
        mp.observe_property("osd-dimensions","native",ui.draw)
        model.refresh()
        timer=mp.add_periodic_timer(1,model.refresh)
        ui.draw()
    end
    return ui
end

