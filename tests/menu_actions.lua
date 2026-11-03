-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Exercise the same temporary bindings that mpv dispatches for a keyboard.
local mode=os.getenv("MPVRP_MENU_TEST")
local actions={"menu","DOWN","DOWN","DOWN","ENTER"}
if mode=="apply" then
    actions[#actions+1]="END"; actions[#actions+1]="UP"; actions[#actions+1]="ENTER"
else actions[#actions+1]="ESC" end
actions[#actions+1]="ESC"
local index=0
mp.add_timeout(.5,function()
    local timer
    timer=mp.add_periodic_timer(.18,function()
        index=index+1
        local key=actions[index]
        if key=="menu" then mp.commandv("script-message-to","rich_presence_conf","menu","Appearance")
        elseif key then mp.commandv("keypress",key)
        else timer:kill(); mp.commandv("quit","0") end
    end)
end)

