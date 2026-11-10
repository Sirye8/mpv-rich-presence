-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Run with an isolated installed config. The caller supplies the capture path.
local root=os.getenv("MPVRP_CAPTURE")
local pages={"Now playing","AniList","Appearance","Recognition & folders","Privacy & windows","Saved data","Diagnostics"}
local step=0
local function menu(page)
    mp.commandv("script-message-to","rich_presence_conf","menu",page)
end
mp.register_event("file-loaded",function()
    mp.add_timeout(0.6,function()
        menu(pages[1])
        local timer
        -- Software video output needs time to present the new overlay.
        timer=mp.add_periodic_timer(1,function()
            step=step+1
            local ok=mp.command_native({"screenshot-to-file",root.."/"..step..".png","window"})
            if step<#pages then menu(pages[step+1])
            else
                timer:kill()
                mp.add_timeout(.2,function() mp.commandv("quit","0") end)
            end
        end)
    end)
end)
