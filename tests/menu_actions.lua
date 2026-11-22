-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Exercise the same temporary bindings that mpv dispatches for a keyboard.
local mode=os.getenv("MPVRP_MENU_TEST")
local actions={"menu","DOWN","DOWN","DOWN","ENTER","DOWN","ENTER"}
if mode=="position" or mode=="inherit-position" then
    actions={"menu","DOWN","DOWN","DOWN","DOWN","ENTER"}
    if mode=="position" then actions[#actions+1]="END"
    else actions[#actions+1]="HOME" end
    actions[#actions+1]="ENTER"
elseif mode=="apply" or mode=="reset-preset" then
    if mode=="reset-preset" then
        actions={"menu","DOWN","DOWN","DOWN","ENTER","ENTER"}
    end
    actions[#actions+1]="END"; actions[#actions+1]="ENTER"
elseif mode=="buttons" then
    -- Appearance → Buttons → Button 1 → Site → Search on Google, then Apply.
    actions={"menu"}
    for _=1,15 do actions[#actions+1]="DOWN" end
    for _,key in ipairs({"ENTER","ENTER","ENTER","END","UP","ENTER","ESC","ESC","END","ENTER"}) do actions[#actions+1]=key end
elseif mode=="custom" then
    actions={"menu","DOWN","DOWN","DOWN","DOWN","DOWN","DOWN","DOWN","DOWN","ENTER","END","ENTER"}
else actions[#actions+1]="ESC" end
actions[#actions+1]="ESC"
local index=0
mp.add_timeout(.5,function()
    local timer
    timer=mp.add_periodic_timer(.18,function()
        index=index+1
        local key=actions[index]
        if key=="menu" then mp.commandv("script-message-to","mpv_rich_presence","menu","Appearance")
        elseif key then mp.commandv("keypress",key)
        else timer:kill(); mp.commandv("quit","0") end
    end)
end)
