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
    remove_key_binding=function(id)
        local key=id:gsub("^rich_presence_sheet_",""):gsub("^rich_presence_ask_","")
        bindings[key]=nil
    end,
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
    assert(bounds[1]<24 and bounds[2]>=42 and bounds[2]<65 and bounds[4]<height*.65 and mouse_enabled)
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
    local count=3
    local function adjust(delta) count=count+delta end
    local stepper=sheet({sections={},animate=false,rows=function()
        return {{id="draft",label="Draft   "..count,stepper=true,adjust=adjust}}
    end,refresh=function() end,closed=function() end})
    stepper.show("Draft test")
    for i=1,2 do
        local control=stepper.hits[i]
        x,y=(control.x+4)*height/paint_height,(control.y+4)*height/paint_height
        mouse_bindings[3][3]()
        mouse_bindings[3][4]()
        assert(count==(i==1 and 2 or 3),"Draft mouse control did not adjust progress")
    end
    bindings.RIGHT()
    assert(count==4 and paint:find("Draft  4",1,true),"Draft did not adjust with Right")
    stepper.close()

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
        if anchor:find("bottom",1,true) then assert(top>height*.4) else assert(top>=42 and top<65) end
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

    local model,job,opened,applied
    local menu=dofile(modules.."/menu.lua")(function(value)
        model=value
        local panel={open=true,page="AniList",draw=function() end,text_columns=function() return 38 end}
        panel.push=function(page) panel.page=page end
        panel.back=function() panel.page="AniList" end
        return panel
    end,function(req)
        job=req.job or job
        if req.action=="apply" then applied=req end
    end,{tmdb_key="",acoustid_key="",
        open_url=function(url) opened=url end},help)
    menu.update({session=8,allowed=true,match=42,title="Fixture anime",episode="2",watch_episode=true,
        tracking={mode="manual"},
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

    menu.page="AniList"
    local proposal={key="5/42/2/0",title="Fixture anime",media=42,episode=2,before=1,
        status="CURRENT",base={id=9,status="PAUSED"}}
    local snapshot={session=8,allowed=true,match=42,title="Fixture anime",episode="2",watch_episode=true,
        account={id=5,name="Viewer",generation="test"},service={proposal=proposal},pending={proposal}}
    menu.update(snapshot)
    find("AniList","Update and switch to Watching").run()
    local function confirmation()
        local text={}
        for _,row in ipairs(model.rows("Confirm")) do if row.plain then text[#text+1]=row.label end end
        return table.concat(text," ")
    end
    assert(confirmation():find("Paused → Watching",1,true))
    find("Confirm","Confirm").run()
    assert(job.op=="accept" and not job.keep_status and job.proposal==proposal.key)
    find("Pending updates","Update progress only").run()
    assert(confirmation():find("Keep Paused for later updates",1,true))
    find("Confirm","Confirm").run()
    assert(job.op=="accept" and job.keep_status)
    proposal.status="COMPLETED"
    proposal.repeat_count=3
    proposal.base={id=9,status="REPEATING",["repeat"]=2}
    menu.update(snapshot)
    find("AniList","Finish rewatch").run()
    assert(confirmation():find("Rewatching → Completed",1,true) and confirmation():find("Rewatches 2 → 3",1,true))

    menu.page="AniList"
    snapshot.tracking={mode="off"}
    snapshot.episode_watch={elapsed=0}
    menu.update(snapshot)
    local watch=find("AniList","Episode watch time")
    assert(watch.label=="Episode watch time   Tracking is off","An empty record hid the off state")
    assert(watch.tip:find("Manual, Ask, or Auto",1,true))
    assert(not find("AniList","Watch started"))
    find("AniList","Tracking").run()
    assert(applied.category=="tracking" and applied.values.mode=="manual")
    snapshot.episode_watch={elapsed=19.2,first_watched=1000,last_watched=20000}
    menu.update(snapshot)
    assert(find("AniList","Episode watch time").label=="Episode watch time   Tracking is off · 00:19 saved")
    assert(find("AniList","Watch started") and find("AniList","Last watched"))
    snapshot.episode_watch={}
    snapshot.tracking.mode="manual"
    menu.update(snapshot)
    assert(find("AniList","Episode watch time").label=="Episode watch time   No time recorded yet")
    local rewatch=find("AniList","Rewatch tracking").tip.options
    assert(rewatch[1].detail:find("episode 1",1,true) and rewatch[2].detail:find("already marked Rewatching",1,true))
    snapshot.tracking.mode="ask"
    menu.update(snapshot)
    assert(find("AniList","Ask timing").label=="Ask timing   At threshold")
    find("AniList","Ask timing").run()
    assert(applied.values.ask_timing=="end")
    assert(find("AniList","Ask prompt position") and find("AniList","Ask prompt timeout"))
    find("AniList","Ask prompt timeout").run()
    assert(menu.page=="Ask prompt timeout")
    find(menu.page,"Never").run()
    assert(applied.values.ask_timeout==0)
    snapshot.tracking.ask_timeout=0
    snapshot.total=12
    snapshot.service.entry={media=42,viewer=5,session=8,progress=3,id=9,status="CURRENT"}
    menu.update(snapshot)
    assert(find("AniList","Ask prompt timeout").label=="Ask prompt timeout   Never")
    local draft=find("AniList","Episodes watched")
    assert(draft.stepper and draft.label=="Episodes watched   3")
    draft.adjust(1)
    assert(find("AniList","Episodes watched").label=="Episodes watched   4")
    draft.adjust(-1)
    assert(find("AniList","Episodes watched").label=="Episodes watched   3")
    snapshot.folder_filter={mode="whitelist",passes=false,current="D:/clips",folders={"D:/anime"}}
    snapshot.record_counts={cache=8,watch=2,match=1}
    snapshot.records={{category="cache",scope="v3:auto:wikidata/abcdef",label="v3:auto:wikidata",expires=1}}
    menu.update(snapshot)
    assert(find("Folder filters","Current folder access").label=="Current folder access   Blocked by filter")
    assert(find("Folder filters","Allow this file until it closes"))
    assert(find("Saved data","Saved caches").label=="Saved caches   8")
    assert(not find("Saved data","cache:v3:auto:wikidata/abcdef"))
    assert(find("Saved caches","Metadata cache"))
    assert(find("Saved: Metadata cache","cache:v3:auto:wikidata/abcdef").label:find("Expired",1,true))

    local popup_model,popup_request
    local ask=dofile(modules.."/ask.lua")(function(value)
        popup_model=value
        return {show=function() end,draw=function() end,close=value.closed}
    end,function(req) popup_request=req end,{menu_animations=false})
    local function popup_row(id)
        for _,item in ipairs(popup_model.rows()) do if item.id==id then return item end end
    end
    proposal.status="CURRENT"
    proposal.repeat_count=nil
    proposal.base={id=9,status="PAUSED"}
    proposal.account="test"
    ask.offer({id="first",session=8,position="bottom-left",seconds=10,timing="threshold",proposal=proposal})
    assert(popup_model.compact and popup_model.position()=="bottom-left")
    assert(popup_model.footer()=="Later · 10 s")
    assert(popup_row("status").label=="Status   Paused → Watching")
    popup_row("keep").run()
    assert(popup_request.ask_prompt=="first" and popup_request.job.op=="accept" and popup_request.job.keep_status)
    assert(not popup_row("update") and popup_model.footer():find("Saving",1,true))
    ask.notice("AniList is offline")
    assert(popup_row("notice").label=="AniList is offline" and popup_row("update"))
    popup_model.closed()
    assert(popup_request.action=="ask_dismiss" and popup_request.id=="first")
    local timer_count=#timers
    ask.offer({id="second",session=8,position="top-right",seconds=0,timing="threshold",proposal=proposal})
    assert(popup_model.footer()=="Later" and #timers==timer_count)
    popup_model.closed()

    width,height=1280,720
    local closed_popup=false
    local compact=sheet({compact=true,sections={},animate=false,position=function() return "top-right" end,
        rows=function() return {{id="update",label="Update AniList",run=function() end}} end,
        footer=function() return "Later · 30 s" end,refresh=function() end,
        closed=function() closed_popup=true end})
    compact.show("AniList update")
    assert(bounds[1]>width*.65 and bounds[3]<width and bounds[2]<height*.15)
    assert(bounds[5]=="rich-presence-ask" and not bindings.DOWN and not bindings.ENTER)
    local close
    for _,item in ipairs(compact.hits) do if item.key=="close" then close=item end end
    assert(close and paint:find("Later · 30 s",1,true))
    x,y=(close.x+4)*height/paint_height,(close.y+4)*height/paint_height
    mouse_bindings[3][3]()
    mouse_bindings[3][4]()
    assert(closed_popup and not compact.open and not mouse_enabled)
end
local ok,err=pcall(verify)
mp=original
if not ok then mp.msg.error(err) end
mp.add_timeout(.15,function() quit("quit",ok and "0" or "1") end)
