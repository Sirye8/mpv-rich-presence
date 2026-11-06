-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Exercise the installed renderer without a Discord or provider connection.
local quit=mp.commandv
local original=mp
local bindings,timers,observed={},{},{}
local updates,removes,closed=0,0,0
local width,height=1280,720
local input
package.loaded["mp.input"]={get=function(value) input=value end}
mp={
    add_forced_key_binding=function(key,id,fn) bindings[key]=fn end,
    remove_key_binding=function(id) bindings[id:gsub("^rich_presence_sheet_","")]=nil end,
    observe_property=function(name,kind,fn) observed[fn]=true end,
    unobserve_property=function(fn) observed[fn]=nil end,
    get_osd_size=function() return width,height end,
    get_property=function() return "old terminal status" end,
    set_property=function() end,
    create_osd_overlay=function() return {update=function() updates=updates+1 end,remove=function() removes=removes+1 end} end,
    add_periodic_timer=function(seconds,fn)
        local timer={killed=false,fn=fn}; timer.kill=function(self) self.killed=true end
        timers[#timers+1]=timer; return timer
    end,
}
local rows={{id="a",label="First",run=function() end},{id="b",label="Second",run=function() end}}
local function verify()
    local sheet=dofile(os.getenv("MPVRP_MODULES").."/sheet.lua")
    local ui=sheet({sections={},rows=function() return rows end,refresh=function() end,
        closed=function() closed=closed+1 end})
    ui.show("Test")
    local before=updates
    ui.draw()
    assert(updates==before,"Unchanged frame was redrawn")
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
    assert(ui.text_columns()==42,"Confirmation width disagrees with the narrow sheet")
    for _,hit in ipairs(ui.hits) do assert(hit.x<hit.r and hit.y<hit.b,"Invalid resized hit target") end
    ui.close()
    assert(next(bindings)==nil and next(observed)==nil and closed==1)
    for _,timer in ipairs(timers) do assert(timer.killed,"Timer remained active after close") end
    before=updates
    ui.draw()
    assert(updates==before,"Closed menu redrew")
end
local ok,err=pcall(verify)
mp=original
if not ok then mp.msg.error(err) end
mp.add_timeout(0,function() quit("quit",ok and "0" or "1") end)
