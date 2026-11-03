-- SPDX-License-Identifier: AGPL-3.0-or-later
return function(make_sheet, send, options)
    local sections={"Now playing","AniList","Appearance","Recognition & folders","Privacy & windows","Saved data","Diagnostics"}
    local snap, draft, pending, scope = {}, {}, nil, "global"
    local layout, replace = "all", false
    local edit, edit_key, edit_base, list_status
    local status, search = "", nil
    local filter=""
    local last_read
    local ui
    local function copy(t) local r={}; for k,v in pairs(t or {}) do r[k]=v end; return r end
    local function request(action,fields)
        local r=fields or {}; r.action=action; r.session=snap.session; send(r)
    end
    local function edits()
        return layout=="all" and draft or {[layout.."_layout"]=draft}
    end
    local function refresh()
        request("snapshot",{nonce="menu",page=ui.page,replace=replace and layout=="all",draft=ui.page=="Appearance" and edits() or nil})
    end
    local function service(op,args)
        args=args or {}; args.op=op
        args.account=(snap.account or {}).generation
        request("service",{job=args})
    end
    local function notice(s) status=s; if ui and ui.open then ui.draw() end end
    local function confirm(label,fn)
        pending={label=label,run=fn,session=snap.session,account=(snap.account or {}).generation}
        ui.push("Confirm")
    end
    local function apply(cat,values,target)
        request("apply",{category=cat,values=values,scope=target or scope,replace=cat=="appearance" and replace and layout=="all"})
        refresh()
    end
    local function pick(page) if page=="Recognition & folders" and scope=="global" then scope="file" end; if page=="Appearance" then draft={}; replace=false end; ui.push(page) end
    local function rows(page)
        local list={}
        local function row(label,fn,primary,adjust)
            list[#list+1]={label=label,run=fn,primary=primary,adjust=adjust,disabled=not fn}
        end
        local function choice(label,values,current,fn)
            local function step(dir)
                local idx=1
                for i,v in ipairs(values) do if v==current then idx=i; break end end
                fn(values[1+(idx-1+dir)%#values])
            end
            row(label.."   "..tostring(current),function() step(1) end,false,step)
        end
        local function scope_row()
            choice("Save for",{"global","folder","file"},scope,function(v) scope=v end)
        end
        local function toggle(label,key,default)
            local v=draft[key]
            local opts=snap.appearance or {}
            if v==nil and layout~="all" then v=(opts[layout.."_layout"] or {})[key] end
            if v==nil then v=opts[key] end
            if v==nil then v=default end
            row(label.."   "..(v and "On" or "Off"),function() draft[key]=not v; refresh() end)
        end
        local account=snap.account or {}
        local answer=snap.service or {}
        if (answer.context and answer.context~=snap.session) or (answer.generation and answer.generation~=account.generation) then answer={} end
        local entry=answer.entry
        local valid_entry=entry and entry.media==snap.match and entry.viewer==account.id and entry.session==snap.session
        local card=snap.card or {}
        local tracking=snap.tracking or {}
        if page=="Menu" then
            for _,title in ipairs(sections) do local dest=title; row(dest,function() pick(dest) end) end
        elseif page=="Confirm" then
            row(pending and pending.label or "Action expired")
            row("Confirm",function()
                local p=pending; pending=nil; ui.back()
                if p and p.session==snap.session and p.account==account.generation then p.run() else notice("Context changed. Review the action again.") end
            end,true)
            row("Cancel",ui.back)
        elseif page=="Now playing" then
            row(snap.reason or "Waiting for mpv")
            row((card.type or "").."   "..(card.name or ""))
            if card.details then row(card.details) end
            if card.state then row(card.state) end
            row("Correct match",function() pick("Recognition & folders") end)
            row(snap.hidden_file and "Share this file" or "Hide this file",function() request("hide_file"); refresh() end)
            row("Use this window for Discord",function() request("claim"); refresh() end)
            if snap.match and snap.match>0 then row(valid_entry and ("AniList progress   "..entry.progress) or "View AniList progress",function() pick("AniList") end) end
        elseif page=="Appearance" then
            local opts=copy(replace and {} or snap.appearance); for k,v in pairs(opts[layout.."_layout"] or {}) do opts[k]=v end; for k,v in pairs(draft) do opts[k]=v end
            row("Preview   "..(card.name or "mpv").."   "..(card.state or card.details or ""))
            scope_row()
            choice("Media layout",{"all","anime","audio","video","movie","tv","recording","stream"},layout,function(v)
                layout=v; draft={}; replace=false; refresh()
            end)
            choice("Preset",{"original","clean","detailed","private"},opts.preset or "original",function(v)
                replace=true; draft={preset=v,group=v~="clean",art=v~="private",badge=v~="private",button=v~="private",
                    timer=v=="private" and "off" or "progress",episode=true,tooltip=true,state=true,details=true,language="original",numbering="relative"}
                refresh()
            end)
            for _,item in ipairs({{"Title language","language",{"original","english","romaji","native"}},
                {"Episode numbering","numbering",{"relative","original","both"}},{"Timer","timer",{"progress","remaining","session","off"}}}) do
                local key=item[2]
                choice(item[1],item[3],opts[key] or item[3][1],function(v) draft[key]=v; refresh() end)
            end
            for _,item in ipairs({{"Release group","group"},{"Episode","episode"},{"Artwork","art"},{"Badge","badge"},
                {"Tooltip","tooltip"},{"AniList button","button"},{"Details","details"},{"State","state"}}) do
                toggle(item[1],item[2],item[2]~="group" or opts.preset~="clean")
            end
            choice("Compact status",{"name","details","state"},opts.compact or "name",function(v) draft.compact=v; refresh() end)
            toggle("Episode titles","spoilers",false)
            for _,item in ipairs({{"Headline","headline"},{"Details template","details_template"},{"State template","state_template"},
                {"Artwork link","large_url"},{"Details link","details_url"},{"State link","state_url"}}) do
                local label,key=item[1],item[2]
                row(label,function() ui.text(label,opts[key] or "",function(v) draft[key]=v; refresh() end) end)
            end
            row("Edit public buttons",function() ui.text("Label|https://host/path (separate two buttons with a comma)","",function(v)
                draft.buttons={}
                for label,url in v:gmatch("([^,|]+)|([^,]+)") do
                    if #draft.buttons<2 then draft.buttons[#draft.buttons+1]={label=label,url=url} end
                end
                refresh()
            end) end)
            row("Apply",function() apply("appearance",edits()); draft={}; replace=false; ui.back() end,true)
            row("Cancel",ui.back)
        elseif page=="Recognition & folders" then
            scope_row()
            choice("Automatic source",{"original","tmdb-movie","tmdb-tv","tvmaze","musicbrainz","stream","acoustid"},
                (snap.sources or {}).source or "original",function(v)
                    confirm("Use "..v.." for "..scope,function() apply("sources",{source=v}) end)
                end)
            row("AniList search",function() ui.text("Search anime","",function(q)
                service("search",{provider="anilist",query=q}); search="anilist"; ui.push("Search results")
            end) end)
            row("Enter AniList ID or URL",function() ui.text("AniList ID or URL","",function(s)
                local id=tonumber(s:match("^%d+$") or s:match("^https://anilist%.co/anime/(%d+)"))
                if id then confirm("Use AniList "..id.." for "..scope,function() apply("match",{id=id,not_anime=false}) end) end
            end) end)
            row("Episode offset   "..((snap.correction or {}).offset or 0),function()
                ui.text("Episode offset","0",function(s) if tonumber(s) then apply("match",{offset=tonumber(s)}) end end)
            end)
            row("Mark as non-anime",function() confirm("Skip anime matching for "..scope,function() apply("match",{not_anime=true}) end) end)
            row("Refresh match",function() request("refresh_match"); refresh() end)
            for _,provider in ipairs({"tmdb-movie","tmdb-tv","tvmaze","musicbrainz","acoustid"}) do
                local id=provider
                row("Search "..id,function() ui.text("Search "..id,"",function(q)
                    service("search",{provider=id,query=q}); search=id; ui.push("Search results")
                end) end)
            end
        elseif page=="Search results" then
            if answer.provider~=search then row("Searching…") else
                for _,r in ipairs(answer.results or {}) do
                    local hit=r
                    row(hit.title,function()
                        confirm("Use "..hit.title.." for "..scope,function()
                            if search=="anilist" then apply("match",{id=hit.id,not_anime=false})
                            else service("select",{candidate=hit,scope=scope}) end
                        end)
                    end)
                end
                if #(answer.results or {})==0 then row("No results") end
            end
        elseif page=="Privacy & windows" then
            scope_row()
            choice("Discord",{"full","generic","hidden"},(snap.privacy or {}).mode or "full",function(v)
                confirm("Set "..scope.." sharing to "..v,function() apply("privacy",{mode=v}) end)
            end)
            row(snap.private and "End private session" or "Private session: stop sharing and tracking",function()
                request("private",{enabled=not snap.private}); refresh()
            end)
            row("Exclude this folder from sharing and tracking",function() confirm("Block lookups, tracking and Discord for this folder",function()
                apply("exclude",{blocked=true},"folder")
            end) end)
            row("Hide Discord for one hour",function() request("hide_hour"); refresh() end)
            row(snap.pinned and "Return to last-resumed window" or "Pin this window for Discord",function()
                request(snap.pinned and "unpin" or "pin"); refresh()
            end)
            row("Use this window",function() request("claim"); refresh() end)
            row("Track while hidden   "..(tracking.without_sharing and "On" or "Off"),function()
                apply("tracking",{without_sharing=not tracking.without_sharing})
            end)
        elseif page=="AniList" then
            row(account.name and ("Connected as "..account.name) or "Not signed in")
            row(account.name and "Switch or reconnect account" or "Connect account",function() service("connect") end)
            if account.name then
                row("Refresh list entry",function() service("read") end)
                row("Disconnect",function() confirm("Disconnect "..account.name,function() service("disconnect") end) end)
            end
            row((snap.title or "").."   Playing episode "..tostring(snap.episode or "?"))
            if valid_entry then
                row(entry.id and ((answer.offline and "Offline · last known   " or "Saved   ")..entry.progress.." / "..(snap.total>0 and snap.total or "?").."   "..entry.status) or "Not on your list")
                local function bump(n) if not edit_base then edit_base=copy(entry) end; edit=math.max(0,(edit or entry.progress)+n); if snap.total>0 then edit=math.min(edit,snap.total) end end
                row("−  One episode",function() bump(-1) end)
                row("Draft   "..tostring(edit or entry.progress))
                row("+  One episode",function() bump(1) end)
                local function submit(target)
                    local message=account.name.." · "..snap.title.." · "..entry.progress.." → "..target
                    if list_status then message=message.." · set status to "..list_status end
                    if not entry.id then message="Create entry · "..message end
                    if target>entry.progress+1 then message="Episode gap · "..message end
                    local base=edit_base or copy(entry)
                    confirm(message,function() service("write",{base=base,target=target,status=list_status}); list_status=nil; edit=nil; edit_base=nil end)
                end
                choice("Status on Apply",{"unchanged","CURRENT","COMPLETED","REPEATING","PAUSED","DROPPED","PLANNING"},
                    list_status or "unchanged",function(v) list_status=v~="unchanged" and v or nil end)
                row("Apply progress",function() submit(edit or entry.progress) end,true)
                row("Reset draft",function() edit=entry.progress; edit_base=copy(entry) end)
                if tonumber(snap.episode) then row("Mark current episode watched",function() submit(tonumber(snap.episode)) end) end
            end
            scope_row()
            choice("Tracking",{"off","manual","ask","auto"},tracking.mode or "off",function(v) apply("tracking",{mode=v}) end)
            row("Resume unfinished time   "..(tracking.resume==false and "Off" or "On"),function()
                apply("tracking",{resume=tracking.resume==false})
            end)
            row("Rewatch tracking   "..(tracking.rewatch and "On" or "Off"),function() apply("tracking",{rewatch=not tracking.rewatch}) end)
            if tracking.rewatch then row("Start new viewing cycle",function() confirm("Rearm tracking for "..(snap.title or ""),function() service("rearm") end) end) end
            row("Threshold   "..(tracking.threshold or 15).." minutes",function()
                ui.text("Minimum elapsed minutes","15",function(s) if tonumber(s) then apply("tracking",{threshold=tonumber(s)}) end end)
            end)
            row(string.format("Eligible time   %02d:%02d",math.floor((answer.elapsed or 0)/60),math.floor(answer.elapsed or 0)%60))
            if answer.proposal then
                local p=answer.proposal
                row("Update episode "..p.episode,function() confirm("Apply "..p.before.." → "..p.episode,function() service("accept",{proposal=p.key}) end) end)
                row("Later",ui.back); row("Skip this episode",function() service("skip",{proposal=p.key}) end)
            end
        elseif page=="Saved data" then
            row("Filter records   "..filter,function() ui.text("Filter by category or scope",filter,function(v) filter=v end) end)
            for _,r in ipairs(snap.records or {}) do
                local cat=r.category; local target=r.scope
                if (cat.." "..target):lower():find(filter:lower(),1,true) then row(cat.."   "..target,function() confirm("Remove local "..cat.." for "..target,function()
                    service("clear",{category=cat,scope=target})
                end) end) end
            end
            for _,cat in ipairs({"cache","watch","pending","history"}) do
                local category=cat
                row("Clear "..cat,function() confirm("Clear local "..cat.." records",function() service("clear",{category=category}) end) end)
            end
            row("Reset everything",function() confirm("Remove local records, disconnect, and turn sharing off",function() service("reset") end) end)
        elseif page=="Diagnostics" then
            row("Sharing   "..(snap.reason or "Waiting")); row("SDK   "..(snap.sdk or "Waiting"))
            row("Configuration   "..(snap.configured and "Received" or "Waiting"))
            row("Publisher   "..(snap.owner and "This window" or "Other window"))
            row("Lookup   "..(snap.allowed and "Allowed" or "Blocked"))
            row("Recognition source   "..tostring(snap.provider or "local"))
            row("AniList ID   "..tostring(snap.match or 0)); row("Preset   "..((snap.appearance or {}).preset or "original"))
            row("Account operation   "..(answer.message or "None"))
        end
        if status~="" then row(status) end
        if page~="Menu" and answer.message then row(answer.message) end
        return list
    end
    ui=make_sheet({sections=sections,rows=rows,refresh=refresh,notice=notice,scale=options.menu_scale,
        leaving=function(page) if page=="Appearance" then draft={}; replace=false end end,
        closed=function() draft={}; edit=nil; edit_base=nil; list_status=nil; pending=nil; status=""; if ui.terminal then mp.set_property("term-status-msg",ui.old_term or "") end end})
    ui.update=function(value)
        if not ui.open then return end
        local key=tostring(value.session).."/"..tostring(value.match).."/"..tostring((value.account or {}).generation)
        if key~=edit_key then edit=nil; edit_base=nil; list_status=nil; pending=nil; draft={}; edit_key=key end
        snap=value
        if ui.page=="AniList" and value.allowed and (value.match or 0)>0 and (value.account or {}).id and last_read~=key then
            last_read=key; service("read")
        end
        ui.draw()
    end
    return ui
end
