-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Exercise the installed renderer without a Discord or provider connection.
local quit=mp.commandv
local original=mp
local bindings,timers,observed={},{},{}
local updates,removes,closed=0,0,0
local clock=0
local width,height=1280,720
local x,y=0,0
local bounds,mouse_enabled,mouse_bindings,paint,paint_height
local margins={}
local input
package.loaded["mp.input"]={get=function(value) input=value end}
mp={
    add_forced_key_binding=function(key,id,fn) bindings[key]=fn end,
    remove_key_binding=function(id) bindings[id:gsub("^rich_presence_sheet_","")]=nil end,
    observe_property=function(name,kind,fn) observed[fn]=true end,
    unobserve_property=function(fn) observed[fn]=nil end,
    get_osd_size=function() return width,height end,
    get_time=function() return clock end,
    get_property=function() return "old terminal status" end,
    get_property_native=function(name,fallback) return name=="user-data/osc/margins" and margins or fallback end,
    set_property=function() end,
    set_key_bindings=function(list) mouse_bindings=list end,
    enable_key_bindings=function() mouse_enabled=true end,
    disable_key_bindings=function() mouse_enabled=false end,
    set_mouse_area=function(...) bounds={...} end,
    get_mouse_pos=function() return x,y end,
    create_osd_overlay=function() return {update=function(self) updates=updates+1; paint=self.data; paint_height=self.res_y end,
        remove=function() removes=removes+1 end} end,
    add_periodic_timer=function(seconds,fn)
        local timer={killed=false,fn=fn,seconds=seconds}; timer.kill=function(self) self.killed=true end
        timers[#timers+1]=timer; return timer
    end,
}
mp.add_timeout=mp.add_periodic_timer
local rows={{id="a",label="First",tip="Description for the first option.",run=function() end},{id="b",label="Second",run=function() end}}
local function verify()
    local modules=os.getenv("MPVRP_MODULES")
    local sheet=dofile(modules.."/sheet.lua")
    local ui=sheet({sections={},animate=false,rows=function() return rows end,refresh=function() end,
        closed=function() closed=closed+1 end})
    ui.show("Test")
    local before=updates
    ui.draw()
    assert(updates==before,"Unchanged frame was redrawn")
    assert(bounds[1]<24 and bounds[2]<24 and bounds[4]<height*.65 and mouse_enabled)
    assert(mouse_bindings[3][1]=="mbtn_left","Pointer clicks must use the scoped mouse section")
    local first=ui.hits[1]
    x,y=(first.x+1)*height/paint_height,(first.y+1)*height/paint_height
    ui.mouse_moved(nil,{hover=true})
    local hover=timers[#timers]
    hover.killed=true
    hover.fn()
    assert(paint:find("Description for the first option.",1,true),"Hover help was not drawn")
    ui.mouse_moved(nil,{hover=false})
    assert(not paint:find("Description for the first option.",1,true),"Hover help survived pointer exit")
    bindings.DOWN()
    assert(ui.memory.Test.focus==2)
    table.insert(rows,1,{id="new",label="Added asynchronously"})
    ui.draw()
    assert(ui.memory.Test.focus==3,"Asynchronous data moved focus to another action")
    local pasted
    ui.text("ID","",function(value) pasted=value end)
    assert(next(bindings)==nil,"Navigation was left active during text entry")
    input.submit("123456 日本語")
    input.closed()
    assert(pasted=="123456 日本語" and bindings.DOWN,"Text entry did not restore controls")
    width,height=480,720
    ui.draw()
    assert(ui.text_columns()>30 and bounds[3]<width,"Narrow sheet exceeded the window")
    for _,hit in ipairs(ui.hits) do assert(hit.x<hit.r and hit.y<hit.b,"Invalid resized hit target") end
    ui.close()
    assert(next(bindings)==nil and next(observed)==nil and closed==1 and not mouse_enabled)
    for _,timer in ipairs(timers) do assert(timer.killed,"Timer remained active after close") end
    before=updates
    ui.draw()
    assert(updates==before,"Closed menu redrew")

    width,height=1280,720
    local builds=0
    local position="top-left"
    local help=dofile(modules.."/menu_help.lua")
    local options=help("Discord","Privacy & windows",true,{"full","generic","hidden"},"generic")
    local animated=sheet({sections={"Recognition & folders"},animate=true,position=function() return position end,
        rows=function()
            builds=builds+1
            return {{id="discord",label="Discord   generic",tip=options,run=function() end}}
        end,
        refresh=function() end,closed=function() end})
    animated.show("Test")
    assert(paint:find("Recognition & folders",1,true),"Sidebar label was truncated")
    local function animation()
        local active
        for _,timer in ipairs(timers) do
            if not timer.killed and timer.seconds<=1/60 then
                assert(not active,"More than one animation ticker")
                active=timer
            end
        end
        return active
    end
    local opening=assert(animation())
    local initial=paint
    clock=clock+.09
    opening.fn()
    assert(builds==1 and paint~=initial,"An animation frame rebuilt the layout or failed to paint")
    clock=clock+.15
    opening.fn()
    assert(not animation(),"Animation remained active after opening")
    assert(paint:find("\\1a&H21&",1,true),"Panel fill is not translucent")
    local hit=animated.hits[2]
    x,y=(hit.x+4)*height/paint_height,(hit.y+4)*height/paint_height
    animated.mouse_moved(nil,{hover=true})
    local wait=timers[#timers]
    wait.killed=true
    wait.fn()
    assert(paint:find("Options",1,true) and paint:find("• Generic",1,true) and paint:find("Hidden",1,true),
        "Choice help did not show distinct options and the current selection")
    clock=clock+.2
    animation().fn()
    assert(not animation(),"Hover animation remained active")
    for _,anchor in ipairs({"top-left","top-center","top-right","bottom-left","bottom-center","bottom-right"}) do
        position=anchor
        animated.draw()
        local left,top,right,bottom=bounds[1],bounds[2],bounds[3],bounds[4]
        assert(left>=0 and right<=width and bottom<height*.88,"Panel leaves the playback area")
        if anchor:find("bottom",1,true) then assert(top>height*.4) else assert(top<24) end
        if anchor:find("center",1,true) then assert(math.abs(left+right-width)<3) end
        if anchor:find("right",1,true) then assert(right>width-24) end
    end
    margins={t=.1,b=.18}
    position="top-left"
    animated.draw()
    assert(bounds[2]>height*.1,"Panel overlapped the OSC title bar")
    position="bottom-right"
    animated.draw()
    assert(bounds[4]<height*.82,"Panel overlapped the bottom OSC")
    margins={}
    animated.push("Next")
    assert(animation())
    animated.close()
    assert(not mouse_enabled and next(bindings)==nil,"Closing kept input captured")
    clock=clock+.05
    animation().fn()
    animated.show("Test")
    clock=clock+.2
    animation().fn()
    assert(animated.open and not animation(),"Rapid reopen failed to settle")
    animated.close()
    clock=clock+.2
    animation().fn()
    assert(not animation() and next(observed)==nil,"Closing left an animation or observer active")

    local full_value = "[SubsPlease] Youjo Senki S2 - 07 (1080p).mkv"
    local long_url = "https://example.com/"..string.rep("longsegment",300).."/end-of-link"
    local text_panel=sheet({sections={},animate=true,rows=function() return {
        {id="details",label="Details   "..full_value,tip="Read-only value."},
        {id="link",label="Link   "..long_url,tip="Open in your default browser. TooltipEndMarker.",run=function() end,link=true},
    } end,refresh=function() end,closed=function() end})
    text_panel.show("Text")
    clock=clock+.21
    animation().fn()
    assert(paint:find(full_value,1,true),"Text was shortened despite having room")
    hit=text_panel.hits[2]
    x,y=(hit.x+4)*height/paint_height,(hit.y+4)*height/paint_height
    text_panel.mouse_moved(nil,{hover=true})
    wait=timers[#timers]
    wait.killed=true
    wait.fn()
    clock=clock+.4
    animation().fn()
    assert(paint:find("\\clip(",1,true) and paint:find(long_url,1,true),"Long row did not scroll with its full text")
    assert(paint:find("Scroll to read the full tooltip",1,true),"Long tooltip did not offer scrolling")
    assert(not paint:find("TooltipEndMarker.",1,true))
    for _=1,25 do mouse_bindings[2][2]() end
    assert(paint:find("TooltipEndMarker.",1,true),"The end of the tooltip could not be reached")
    text_panel.close()
    clock=clock+.2
    animation().fn()

    local model,job,opened
    local menu=dofile(modules.."/menu.lua")(function(value)
        model=value
        local panel={open=true,page="AniList",draw=function() end,text_columns=function() return 38 end}
        panel.push=function(page) panel.page=page end
        return panel
    end,function(req) job=req.job or job end,{tmdb_key="",acoustid_key="",
        open_url=function(url) opened=url end},help)
    menu.update({session=8,allowed=true,match=42,title="Fixture anime",episode="2",watch_episode=true,
        account={id=5,name="Viewer",generation="test"},episode_watch={elapsed=100.4,first_watched=1000,last_watched=103000},
        card={details="Fixture details",state="Fixture state",start=1,["end"]=2,
            buttons={{label="View on AniList",url="https://anilist.co/anime/42"}}},
        service={message="Progress refreshed.",elapsed=999}})
    assert(job and job.op=="read" and job.quiet,"Automatic list read sent a normal notification")
    local function find(page,id)
        for _,row in ipairs(model.rows(page)) do if row.id==id then return row end end
    end
    assert(find("AniList","Episode watch time").label=="Episode watch time   01:40","Watch time was not read from the current record")
    assert(find("AniList","Watch started") and find("AniList","Last watched"))
    assert(not find("Card preview","start") and not find("Card preview","end"),"Discord timestamps leaked into the preview")
    assert(find("Card preview","Details").tip:match("^Read only%.") and type(find("Appearance","Details").tip)=="table",
        "Preview and editable help were mixed")
    find("Card preview","Public button 1").run()
    assert(opened=="https://anilist.co/anime/42","Preview link did not open its URL")
    assert(find("Providers","TMDB attribution").label=="This product uses the TMDB API but is not endorsed or certified by TMDB.")
    assert(not find("AniList","Message"),"A saved worker message was repeated")
    model.notice("Disconnected.","AniList")
    assert(find("AniList","Message").label=="Disconnected.")
    menu.page="Privacy & windows"
    model.changed()
    model.notice("Progress refreshed.","AniList")
    assert(not find("Privacy & windows","Message"),"Account notice appeared on a different page")
    find("Disconnect account","Forget local watch history").run()
    assert(menu.page=="Confirm")
    local parts={}
    for _,row in ipairs(model.rows("Confirm")) do if row.plain then parts[#parts+1]=row.label end end
    assert(table.concat(parts," "):find("forget local history?",1,true),"Confirmation split a word")
end
local ok,err=pcall(verify)
mp=original
if not ok then mp.msg.error(err) end
mp.add_timeout(.15,function() quit("quit",ok and "0" or "1") end)
