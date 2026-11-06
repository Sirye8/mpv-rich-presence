-- This file is part of mpv-rich-presence.
-- Copyright (c) 2026 Alden Wu.
-- Copyright (c) 2026 Youssef M. (Sirye8).
--
-- mpv-rich-presence is free software: you can redistribute it and/or modify it
-- under the terms of the GNU Affero General Public License as published by the
-- Free Software Foundation, either version 3 of the License, or (at your
-- option) any later version.
--
-- mpv-rich-presence is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
-- FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
-- for more details.
--
-- You should have received a copy of the GNU Affero General Public License
-- along with mpv-rich-presence. If not, see <https://www.gnu.org/licenses/>.

local mp_msg = require("mp.msg")
local mp_options = require("mp.options")

local utils = require("mp.utils")
local options = {
    appearance_preset = "original",
    menu_key = "Alt+p",
    menu_scale = 1,
    anilist_personal_client = false,
    anilist_exchange_url = "",
    anilist_client_id = "55822",
    python = package.config:sub(1,1) == "\\" and "python" or "python3",
    tmdb_key = "",
    acoustid_key = "",
    application_id = "0",
    on = false,
    folder_filter_mode = "disabled",
    folder_list = "",
    pause_timeout = 1800,
}
mp_options.read_options(options, "rich-presence")

local base = mp.command_native({"expand-path", "~~home/"})
local database = utils.join_path(base, "rich-presence/data.sqlite3")
local modules = utils.join_path(base, "script-modules/mpv-rich-presence")
local function panel(req)
    local encoded = utils.format_json(req)
    if encoded then mp.commandv("script-message-to","rich_presence","panel",encoded) end
end

function pong()
    mp_msg.info("Ponging rich_presence with config data...")
    mp.commandv("script-message-to", "rich_presence", "application_id", options.application_id)

    if options.on then
        mp.commandv("script-message-to", "rich_presence", "on")
    else
        mp.commandv("script-message-to", "rich_presence", "off")
    end

    mp.commandv("script-message-to", "rich_presence", "folder_filter_mode", options.folder_filter_mode)
    mp.commandv("script-message-to", "rich_presence", "folder_list", options.folder_list)
    mp.commandv("script-message-to", "rich_presence", "pause_timeout", tostring(options.pause_timeout))
    panel({action="setup",database=database,preset=options.appearance_preset})
    mp.commandv("script-message-to", "rich_presence", "config_complete")
end

mp.register_script_message("ping", pong)
pong()


local worker
mp.register_script_message("start-service", function(instance)
    if worker then return end
    -- These settings are public. Tokens and provider keys never pass through argv.
    -- mpv does not reliably feed subprocess stdin on Windows.
    local settings = utils.format_json({broker=options.anilist_exchange_url,
        personal=options.anilist_personal_client,client_id=options.anilist_client_id})
    local args = {options.python, "-I", utils.join_path(modules,"backend.py"), database, instance, settings}
    worker = mp.command_native_async({name="subprocess",args=args,playback_only=false,
        capture_stderr=true}, function(_,result)
        worker=nil
        if not result or result.status~=0 then
            local failure = result and (result.stderr or ""):match("([%a]+Error):")
            mp.msg.warn("Account helper unavailable ("..tostring(result and result.status or "launch failed")..
                (failure and ", "..failure or "").."). Check the Python setting.")
        end
    end)
end)
mp.register_event("shutdown",function() if worker then mp.abort_async_command(worker) end end)

local ok, menu = pcall(function()
    local sheet = dofile(utils.join_path(modules,"sheet.lua"))
    return dofile(utils.join_path(modules,"menu.lua"))(sheet,panel,options)
end)
if ok then
    mp.add_key_binding(options.menu_key,"rich-presence-menu",function() menu.show() end)
    mp.register_script_message("menu",function(page) menu.show(page) end)
    mp.register_script_message("notice",menu.toast)
    mp.register_script_message("snapshot",function(raw)
        local data=utils.parse_json(raw)
        if data then menu.update(data) end
    end)
else
    mp.msg.warn("Could not load the Rich Presence menu: "..tostring(menu))
end
