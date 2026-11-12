-- SPDX-License-Identifier: AGPL-3.0-or-later
return function(make_sheet, send, options, help)
    local sections={"Now playing","AniList","Appearance","Recognition & folders","Privacy & windows","Saved data","Diagnostics"}
    local sources={
        {"auto","Automatic"},{"anime","Anime"},{"movie-tv","Movies & TV"},{"movie","Movies"},{"tv","TV"},
        {"original","Original"},{"tmdb-movie","TMDB movies"},{"tmdb-tv","TMDB TV"},{"tvmaze","TVmaze"},
        {"wikidata","Wikidata"},{"musicbrainz","MusicBrainz"},{"acoustid","AcoustID"},{"stream","Stream metadata"},
    }
    local snap, draft, pending, scope = {}, {}, nil, "global"
    local layout, replace = "all", false
    local edit, edit_key, edit_base, list_status
    local status, status_timer, search
    local filter=""
    local last_read
    local record
    local ui
    local panel_positions={"config","top-left","top-center","top-right","bottom-left","bottom-center","bottom-right"}
    local config_position="top-left"
    for _,position in ipairs(panel_positions) do
        if position~="config" and position==options.panel_position then config_position=position end
    end
    local function clock_time(ms)
        return ms and ms>0 and os.date("%d %b, %H:%M:%S",math.floor(ms/1000)) or "Not recorded"
    end
    local function duration(seconds)
        local n=math.floor(seconds)
        if n<3600 then return string.format("%02d:%02d",math.floor(n/60),n%60) end
        return string.format("%d:%02d:%02d",math.floor(n/3600),math.floor(n/60)%60,n%60)
    end
    local function copy(t) local r={}; for k,v in pairs(t or {}) do r[k]=v end; return r end
    local function request(action,fields)
        local r=fields or {}; r.action=action; r.session=snap.session; send(r)
    end
    local function edits()
        return layout=="all" and draft or {[layout.."_layout"]=draft}
    end
    local function refresh()
        local preview=ui.page=="Appearance" or ui.page=="Card preview"
        request("snapshot",{nonce="menu",page=ui.page,replace=preview and replace and layout=="all",draft=preview and edits() or nil})
    end
    local function service(op,args)
        args=args or {}; args.op=op
        args.account=(snap.account or {}).generation
        args.notice_page=args.notice_page or ui.page
        request("service",{job=args})
    end
    local function clear_notice()
        if status_timer then status_timer:kill(); status_timer=nil end
        status=nil
    end
    local function notice(message,page)
        if page and page~="" and page~=ui.page then return end
        clear_notice()
        status=message
        status_timer=mp.add_timeout(3.5,function()
            status,status_timer=nil,nil
            ui.draw()
        end)
        ui.draw()
    end
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
        local function row(label,fn,primary,adjust,id)
            list[#list+1]={label=label,run=fn,primary=primary,adjust=adjust,disabled=not fn,
                id=id or label:match("^(.-)   ") or label,tip=help(label,page,fn~=nil,nil,nil,id)}
            return list[#list]
        end
        local function link(label,url,description,detail,id)
            local item=row(label.."   "..(detail or url),function() options.open_url(url) end,false,nil,id)
            item.link=true
            item.tip=description.." Click or press Enter to open "..(detail and url.." " or "").."in your default browser."
        end
        local function choice(label,values,current,fn,display)
            local function step(dir)
                local idx=1
                for i,v in ipairs(values) do if v==current then idx=i; break end end
                fn(values[1+(idx-1+dir)%#values])
            end
            local item=row(label.."   "..(display or tostring(current)),function() step(1) end,false,step)
            item.tip=help(label,page,true,values,current)
        end
        local function switch(label,v,fn)
            local value=v and "On" or "Off"
            local item=row(label.."   "..value,fn)
            item.toggle=v
            item.tip=help(label,page,true,{"On","Off"},value)
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
            switch(label,v,function() draft[key]=not v; refresh() end)
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
            -- Keep the entire target visible, including long folder names and titles.
            local text=pending and pending.label or "Action expired"
            local width=ui.text_columns()
            local line,units,index="",0,0
            local function add_line()
                index=index+1
                row(line,nil,false,nil,"Confirmation "..index).plain=true
                line,units="",0
            end
            for word in text:gmatch("%S+") do
                local length=0
                for ch in word:gmatch("[%z\1-\127\194-\244][\128-\191]*") do length=length+(#ch>2 and 2 or 1) end
                if units>0 and units+length+1>width then add_line() end
                if units>0 then line=line.." "; units=units+1 end
                if length<=width then line=line..word; units=units+length
                else
                    for ch in word:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
                        local size=#ch>2 and 2 or 1
                        if units+size>width then add_line() end
                        line=line..ch; units=units+size
                    end
                end
            end
            if line~="" then add_line() end
            row("Confirm",function()
                local p=pending; pending=nil; ui.back()
                if p and p.session==snap.session and p.account==account.generation then p.run() else notice("Context changed. Review the action again.") end
            end,true)
            row("Cancel",ui.back)
        elseif page=="Now playing" then
            row("Sharing   "..(snap.reason or "Waiting for mpv"))
            row("Activity   "..(card.type or "").." "..(card.name or ""))
            if card.details then row("Details   "..card.details) end
            if card.state then row("State   "..card.state) end
            row("View outgoing card",function() draft={}; replace=false; ui.push("Card preview") end)
            row("Correct match",function() pick("Recognition & folders") end)
            row(snap.hidden_file and "Share this file" or "Hide this file",function() request("hide_file"); refresh() end)
            row("Use this window for Discord",function() request("claim"); refresh() end)
            if snap.match and snap.match>0 then row(valid_entry and ("AniList progress   "..entry.progress) or "View AniList progress",function() pick("AniList") end) end
        elseif page=="Card preview" then
            row("Activity   "..(card.type or "").." "..(card.name or ""))
            for _,field in ipairs({{"Details","details"},{"State","state"},{"Artwork","large_image"},{"Artwork tooltip","large_text"},
                {"Badge","small_image"},{"Badge tooltip","small_text"},{"Compact status","compact"},
                {"Artwork link","large_url"},{"Details link","details_url"},{"State link","state_url"}}) do
                local value=card[field[2]]
                if value then
                    if type(value)=="string" and value:match("^https?://") then
                        link(field[1],value,"This URL is part of the outgoing Discord card.")
                    else row(field[1].."   "..value) end
                end
            end
            for i,button in ipairs(card.buttons or {}) do
                link(button.label,button.url,"This button label and URL appear on the Discord card.",nil,"Public button "..i)
            end
        elseif page=="Appearance" then
            local opts=copy(replace and {} or snap.appearance); for k,v in pairs(opts[layout.."_layout"] or {}) do opts[k]=v end; for k,v in pairs(draft) do opts[k]=v end
            row("Preview   "..(card.name or "mpv").." · "..(card.state or card.details or ""),function() ui.push("Card preview") end)
            scope_row()
            choice("Media layout",{"all","anime","audio","video","movie","tv","recording","stream"},layout,function(v)
                layout=v; draft={}; replace=false; refresh()
            end)
            choice("Preset",{"original","clean","detailed","private"},opts.preset or "original",function(v)
                replace=true; draft={preset=v,group=v~="clean",art=v~="private",badge=v~="private",button=v~="private",
                    timer=v=="private" and "off" or "progress",episode=true,tooltip=true,state=true,details=true,language="original",numbering="relative"}
                refresh()
            end)
            local position=snap.panel_position
            if not position or position=="" then position="config" end
            choice("Panel position",panel_positions,position,function(v)
                if v=="config" then request("clear",{category="panel",scope="global"}); refresh()
                else apply("panel",{position=v},"global") end
            end,position=="config" and "Config · "..config_position or nil)
            for _,item in ipairs({{"Title language","language",{"original","english","romaji","native"}},
                {"Episode numbering","numbering",{"relative","original","both"}},{"Timer","timer",{"progress","remaining","session","off"}}}) do
                local key=item[2]
                choice(item[1],item[3],opts[key] or item[3][1],function(v) draft[key]=v; refresh() end)
            end
            for _,item in ipairs({{"Release group","group"},{"Episode","episode"},{"Artwork","art"},{"Badge","badge"},
                {"Tooltip","tooltip"},{"Provider button","button"},{"Details","details"},{"State","state"}}) do
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
            row("Reset saved appearance",function()
                confirm("Clear appearance overrides for "..scope.." and show the inherited card",function()
                    request("clear",{category="appearance",scope=scope}); draft={}; replace=false; refresh()
                end)
            end)
            row("Apply",function() apply("appearance",edits()); draft={}; replace=false; ui.back() end,true)
            row("Cancel",ui.back)
        elseif page=="Recognition & folders" then
            scope_row()
            local current=(snap.sources or {}).source or "auto"
            local label=current
            for _,source in ipairs(sources) do if source[1]==current then label=source[2]; break end end
            if scope=="folder" and not snap.has_local_folder then
                row("Open a local file to set its folder source")
            else
                row("Media recognition   "..label,function() ui.push("Choose source") end)
            end
            row("Source rule   "..(snap.source_scope or "configuration"))
            row("Provider settings and credits",function() ui.push("Providers") end)
            row("AniList search",function() ui.text("Search anime","",function(q)
                service("search",{provider="anilist",query=q,notice_page="Search results"}); search="anilist"; ui.push("Search results")
            end) end)
            row("Enter AniList ID or URL",function() ui.text("AniList ID or URL","",function(s)
                local id=tonumber(s:match("^%d+$") or s:match("^https://anilist%.co/anime/(%d+)"))
                if id then confirm("Use AniList "..id.." for "..scope,function() apply("match",{id=id,not_anime=false}) end) end
            end) end)
            local offset=(snap.correction or {}).offset or 0
            row("Episode offset   "..offset,function()
                ui.text("Episode offset",tostring(offset),function(s)
                    local value=tonumber(s)
                    if value and value==math.floor(value) and value>=-9999 and value<=9999 then apply("match",{offset=value})
                    else notice("Enter a whole number from -9999 to 9999.") end
                end)
            end)
            row("Mark as non-anime",function() confirm("Skip anime matching for "..scope,function() apply("match",{not_anime=true}) end) end)
            row("Refresh match",function() request("refresh_match"); refresh() end)
            for _,provider in ipairs({{"tmdb-movie","TMDB movies"},{"tmdb-tv","TMDB TV"},{"tvmaze","TVmaze"},{"musicbrainz","MusicBrainz"}}) do
                local id,name=provider[1],provider[2]
                row("Search "..name,function() ui.text("Search "..name,"",function(q)
                    service("search",{provider=id,query=q,notice_page="Search results"}); search=id; ui.push("Search results")
                end) end)
            end
            row("Fingerprint this file",function()
                service("search",{provider="acoustid",notice_page="Search results"}); search="acoustid"; ui.push("Search results")
            end)
        elseif page=="Choose source" then
            row("Inherit recognition source",function()
                confirm("Remove the recognition override for "..scope,function()
                    request("clear",{category="sources",scope=scope}); refresh(); ui.back()
                end)
            end)
            for _,source in ipairs(sources) do
                local value,label=source[1],source[2]
                row(label,function()
                    confirm("Use "..label.." for "..scope,function()
                        apply("sources",{source=value}); ui.back()
                    end)
                end)
            end
        elseif page=="Providers" then
            link("AniList","https://anilist.co/","Supplies public anime titles, artwork and sequel links. Sign-in is only needed for list features.","Anime metadata")
            link("TMDB","https://www.themoviedb.org/","Supplies movie and TV titles, episode metadata and posters. The packaged default uses the shared service.",
                options.tmdb_key~="" and "Personal token" or "Shared service")
            row("This product uses the TMDB API but is not endorsed or certified by TMDB.",nil,false,nil,"TMDB attribution")
            link("TVmaze","https://www.tvmaze.com/","Supplies aired episode order and artwork as a TV fallback. Its data is licensed under CC BY-SA.","TV metadata · CC BY-SA")
            link("Wikidata","https://www.wikidata.org/","Supplies film titles and release years if TMDB cannot resolve a movie.","Movie fallback")
            link("MusicBrainz","https://musicbrainz.org/","Matches recording and album tags. The plugin checks artist credits and runtime before selecting a recording.","Music metadata")
            link("Cover Art Archive","https://coverartarchive.org/","Supplies artwork for identified MusicBrainz releases and release groups.","Album artwork")
            link("AcoustID","https://acoustid.org/","Identifies local audio using a fingerprint. Only the fingerprint and duration are sent. The package includes an application key.",
                options.bundled_fpcalc and "Fingerprinting included" or "Uses fpcalc on PATH")
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
            row(snap.private and "End private session" or "Start private session",function()
                request("private",{enabled=not snap.private}); refresh()
            end)
            row("Exclude this folder",function() confirm("Block lookups, tracking and Discord for this folder?",function()
                apply("exclude",{blocked=true},"folder")
            end) end)
            row("Hide Discord for a time",function()
                ui.text("Minutes to hide (0 to resume)","60",function(value)
                    local minutes=tonumber(value)
                    if minutes and minutes==math.floor(minutes) and minutes>=0 and minutes<=1440 then
                        request("hide_minutes",{minutes=minutes}); refresh()
                    end
                end)
            end)
            row(snap.hidden_until_exit and "End window hide" or "Hide Discord until this window closes",function()
                request("hide_until_exit",{enabled=not snap.hidden_until_exit}); refresh()
            end)
            row(snap.pinned and "Return to last-resumed window" or "Pin this window for Discord",function()
                request(snap.pinned and "unpin" or "pin"); refresh()
            end)
            row("Use this window",function() request("claim"); refresh() end)
            switch("Track while hidden",tracking.without_sharing==true,function()
                apply("tracking",{without_sharing=not tracking.without_sharing})
            end)
        elseif page=="AniList" then
            row("Account   "..(account.name or "Not signed in"))
            row(account.expired and "Reconnect to AniList" or account.name and "Switch or reconnect account" or "Connect account",function() service("connect") end)
            if not account.name then
                row("Connect for this session",function() service("connect",{session_only=true}) end)
            end
            if account.name then
                row("Refresh list entry",function() service("read") end)
                row("Disconnect",function() ui.push("Disconnect account") end)
            end
            row("Anime   "..((snap.match or 0)>0 and (snap.title or "Unknown title") or "No match"))
            row("Playing episode   "..(snap.episode and snap.episode~="" and snap.episode or "Unknown"))
            local watch=snap.episode_watch or {}
            if not account.id then row("Episode watch time   Connect AniList")
            elseif not snap.allowed then row("Episode watch time   Lookup blocked")
            elseif (snap.match or 0)==0 then row("Episode watch time   No anime match")
            elseif not snap.episode or snap.episode=="" then row("Episode watch time   No episode number")
            elseif not snap.watch_episode then row("Episode watch time   Episode needs review")
            elseif (tracking.mode or "off")=="off" and not watch.elapsed then row("Episode watch time   Tracking is off")
            else
                row("Episode watch time   "..duration(watch.elapsed or 0))
                row("Watch started   "..clock_time(watch.first_watched))
                row("Last watched   "..clock_time(watch.last_watched))
            end
            if answer.read_state=="loading" then row("List status   Loading…") end
            if answer.read_state=="offline" and not valid_entry then row("List status   Offline · no confirmed progress") end
            if valid_entry then
                row(entry.id and ("List entry   "..entry.progress.." / "..(snap.total>0 and snap.total or "?")..
                    " · "..entry.status..(answer.offline and " · Offline" or "")) or "List status   Not on your list")
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
            switch("Resume unfinished time",tracking.resume~=false,function()
                apply("tracking",{resume=tracking.resume==false})
            end)
            switch("Keep offline proposals",tracking.keep_pending==true,function()
                apply("tracking",{keep_pending=not tracking.keep_pending})
            end)
            row("Pending updates   "..#(snap.pending or {}),function() ui.push("Pending updates") end)
            switch("Rewatch tracking",tracking.rewatch==true,function() apply("tracking",{rewatch=not tracking.rewatch}) end)
            if tracking.rewatch then row("Start new viewing cycle",function() confirm("Rearm tracking for "..(snap.title or ""),function() service("rearm") end) end) end
            local threshold=tracking.threshold or 15
            row("Threshold   "..threshold.." minutes",function()
                ui.text("Minimum elapsed minutes",tostring(threshold),function(s)
                    local value=tonumber(s)
                    if value and value==math.floor(value) and value>=1 and value<=1440 then apply("tracking",{threshold=value})
                    else notice("Enter a whole number from 1 to 1440 minutes.") end
                end)
            end)
            if answer.proposal then
                local p=answer.proposal
                row("Update episode "..p.episode,function() confirm("Apply "..p.before.." → "..p.episode,function() service("accept",{proposal=p.key}) end) end)
                row("Later",ui.back); row("Skip this episode",function() service("skip",{proposal=p.key}) end)
            end
        elseif page=="Disconnect account" then
            if not account.name then row("Account   Not signed in"); return list end
            row("Account   "..account.name)
            row("Keep local watch history",function()
                confirm("Disconnect "..account.name.."? Keep local watch history and cancel queued updates.",function()
                    service("disconnect",{notice_page="AniList"}); ui.back()
                end)
            end)
            row("Forget local watch history",function()
                confirm("Disconnect "..account.name.." and forget local history? This clears local watch times, cached progress and pending updates.",function()
                    service("disconnect",{forget_history=true,notice_page="AniList"}); ui.back()
                end)
            end)
        elseif page=="Pending updates" then
            for _,item in ipairs(snap.pending or {}) do
                local prop=item
                local label=(prop.title~="" and prop.title or ("AniList "..prop.media)).." · Episode "..prop.episode
                row(label,nil,false,nil,"Proposal "..prop.key)
                if prop.offline then row("Waiting to refresh remote progress")
                elseif snap.allowed and prop.media==snap.match and prop.episode==tonumber(snap.episode) then
                    row("Apply "..prop.before.." → "..prop.episode,function() confirm(label.." · "..prop.before.." → "..prop.episode,function()
                        service("accept",{proposal=prop.key})
                    end) end)
                else row("Play this episode to apply its update") end
                row("Skip "..label,function() confirm("Skip "..label,function() service("skip",{proposal=prop.key}) end) end)
            end
            if #(snap.pending or {})==0 then row("No pending updates") end
        elseif page=="Saved data" then
            row("Filter records   "..filter,function() ui.text("Filter by category or scope",filter,function(v) filter=v end) end)
            local counts={}
            for _,r in ipairs(snap.records or {}) do counts[r.category]=(counts[r.category] or 0)+1 end
            for _,r in ipairs(snap.records or {}) do
                local cat=r.category; local target=r.scope
                if (cat.." "..target):lower():find(filter:lower(),1,true) then
                    row(cat.."   "..target,function()
                        if cat=="match" then record=r; ui.push("Saved match"); return end
                        confirm("Remove local "..cat.." for "..target,function()
                            service("clear",{category=cat,scope=target})
                        end)
                    end,false,nil,cat..":"..target)
                end
            end
            for _,cat in ipairs({"cache","watch","pending","skipped","history","entry"}) do
                local category=cat
                local count=counts[category] or 0
                local effect=category=="watch" and " Restart unfinished timers." or category=="pending" and " Dismiss these proposals." or ""
                row("Clear "..cat.." ("..count..")",function() confirm("Remove "..count.." local "..cat.." records."..effect,function() service("clear",{category=category}) end) end)
            end
            row("Reset everything",function() confirm("Clear all local profiles, matches, caches and watch history; disconnect and turn sharing off",function() service("reset") end) end)
        elseif page=="Saved match" and record then
            row("Scope   "..record.scope)
            for _,field in ipairs({{"id","Pinned AniList ID"},{"not_anime","Non-anime flag"},{"offset","Episode offset"}}) do
                local key,label=field[1],field[2]
                if record.fields[key]~=nil then
                    row(label.."   "..tostring(record.fields[key]),function()
                        local target=record.scope
                        confirm("Forget "..label.." for "..target..". Other fields stay saved.",function()
                            service("clear",{category="match",scope=target,field=key}); record.fields[key]=nil
                        end)
                    end)
                end
            end
        elseif page=="Diagnostics" then
            row("Sharing   "..(snap.reason or "Waiting")); row("SDK   "..(snap.sdk or "Waiting"))
            row("Configuration   "..(snap.configured and "Received" or "Waiting"))
            row("Publisher   "..(snap.owner and "This window" or "Other window"))
            row("Lookup   "..(snap.allowed and "Allowed" or "Blocked"))
            row("Recognition source   "..tostring(snap.provider or "local"))
            row("Source rule   "..(snap.source_scope or "configuration"))
            row("Match rule   "..(snap.match_scope or "configuration"))
            row("Appearance rule   "..(snap.appearance_scope or "configuration"))
            row("Tracking   "..(snap.track_allowed and "Eligible" or "Off or excluded"))
            if answer.last_read then row("Last list read   "..os.date("%H:%M:%S",math.floor(answer.last_read))) end
            if answer.last_write then row("Last list write   "..os.date("%H:%M:%S",math.floor(answer.last_write))) end
            row("AniList ID   "..tostring(snap.match or 0)); row("Preset   "..((snap.appearance or {}).preset or "original"))
            row("Account operation   "..(answer.message or "None"))
        end
        if status then row(status,nil,false,nil,"Message") end
        return list
    end
    ui=make_sheet({sections=sections,rows=rows,refresh=refresh,notice=notice,scale=options.menu_scale,
        animate=options.menu_animations,help=help,changed=clear_notice,position=function()
            return snap.panel_position and snap.panel_position~="" and snap.panel_position or config_position
        end,
        leaving=function(page) if page=="Appearance" then draft={}; replace=false end end,
        closed=function() draft={}; edit=nil; edit_base=nil; list_status=nil; pending=nil; clear_notice(); if ui.terminal then mp.set_property("term-status-msg",ui.old_term or "") end end})
    ui.update=function(value)
        if not ui.open then return end
        local key=tostring(value.session).."/"..tostring(value.match).."/"..tostring((value.account or {}).generation)
        if key~=edit_key then edit=nil; edit_base=nil; list_status=nil; pending=nil; draft={}; edit_key=key end
        snap=value
        if ui.page=="AniList" and value.allowed and (value.match or 0)>0 and (value.account or {}).id and last_read~=key then
            last_read=key; service("read",{quiet=true})
        end
        ui.draw()
    end
    return ui
end
