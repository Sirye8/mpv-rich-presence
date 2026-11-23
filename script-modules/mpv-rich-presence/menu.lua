-- SPDX-License-Identifier: AGPL-3.0-or-later
return function(make_sheet, send, options, help)
    local sections={"Now playing","AniList","Last.fm","Appearance","Recognition & folders","Privacy & windows","Saved data","Diagnostics"}
    local sources={
        {"auto","Automatic"},{"anime","Anime"},{"movie-tv","Movies & TV"},{"movie","Movies"},{"tv","TV"},
        {"original","Original"},{"local","Local only"},{"tmdb-movie","TMDB movies"},{"tmdb-tv","TMDB TV"},{"tvmaze","TVmaze"},
        {"wikidata","Wikidata"},{"musicbrainz","MusicBrainz"},{"acoustid","AcoustID"},{"stream","Stream metadata"},
    }
    local snap, draft, pending = {}, {}, nil
    local scopes={}
    local layout, replace = "all", false
    local ui
    local themes={
        glass={background="030405",accent="AFD9FF",opacity=87,blur=true,strength=6},
        midnight={background="0B1323",accent="A7D0FF",opacity=82,blur=true,strength=8},
        graphite={background="101113",accent="C9DEDA",opacity=100,blur=false,strength=6},
        contrast={background="000000",accent="FFE14D",opacity=100,blur=false,strength=6,contrast=true},
    }
    local theme_names={glass="Dark glass",midnight="Midnight",graphite="Graphite",contrast="High contrast",custom="Custom"}
    local blur_names={classic="Classic",gaussian="Gaussian",box="Box"}
    local layout_names={all="All media",anime="Anime",audio="Audio",video="Video",movie="Movies",
        tv="TV",recording="Music recordings",stream="Streams"}
    local source_help={inherit="Use the inherited recognition settings.",auto="Match anime, movies, TV or music automatically.",
        anime="Use AniList for anime.",["movie-tv"]="Look for movies and TV episodes.",movie="Look for movies only.",tv="Look for TV only.",
        original="Use the original local card and AniList matching.",["local"]="Use local metadata without a new provider search.",
        ["tmdb-movie"]="Look for movies with TMDB.",["tmdb-tv"]="Look for TV with TMDB.",tvmaze="Look for TV with TVmaze.",
        wikidata="Look for movies with Wikidata.",musicbrainz="Use MusicBrainz for music tags.",acoustid="Use AcoustID for local audio fingerprints.",
        stream="Use public metadata from supported streams."}
    local layout_help={all="Use the defaults for every media type.",anime="Override the card for recognized anime.",
        audio="Override the card for local audio without a recording match.",video="Override the card for local or unmatched video.",
        movie="Override the card for recognized movies.",tv="Override the card for recognized TV episodes.",
        recording="Override the card for matched music recordings.",stream="Override the card for supported streams."}
    local function appearance_edit(key,value)
        draft[key]=value
        draft.custom=true
    end
    local button_sites={off="Off",provider="Matched provider",custom="Custom link",anilist="AniList",
        myanimelist="MyAnimeList",tmdb="TMDB",imdb="IMDb",tvmaze="TVmaze",wikidata="Wikidata",musicbrainz="MusicBrainz",
        lastfm="Last.fm",stream="Stream page",["search-youtube"]="Search on YouTube",["search-google"]="Search on Google"}
    local button_site_help={off="No button in this position.",
        provider="Link to the entry that recognition matched: AniList, TMDB, TVmaze, Wikidata, MusicBrainz or the stream page.",
        custom="Your own label and public https link. Placeholders in the link are URL-encoded.",
        anilist="The matched AniList anime page.",myanimelist="The MyAnimeList page linked from the AniList entry.",
        tmdb="The matched TMDB movie or show.",imdb="The IMDb title linked from the TMDB match.",
        tvmaze="The matched TVmaze show.",wikidata="The matched Wikidata film.",musicbrainz="The matched MusicBrainz recording or release.",
        lastfm="The Last.fm track page, built from the recognized artist and title.",stream="The public YouTube or Twitch page.",
        ["search-youtube"]="A YouTube search for the title, even without a match.",["search-google"]="A Google search for the title, even without a match."}
    local layout_sites={all={"anilist","myanimelist","tmdb","imdb","tvmaze","wikidata","lastfm","musicbrainz","stream"},
        anime={"anilist","myanimelist"},movie={"tmdb","imdb","wikidata"},tv={"tmdb","imdb","tvmaze"},
        recording={"lastfm","musicbrainz"},audio={"lastfm"},video={},stream={"stream"}}
    local function button_values(with_custom)
        local values={"off","provider"}
        for _,site in ipairs(layout_sites[layout] or {}) do values[#values+1]=site end
        values[#values+1]="search-youtube"; values[#values+1]="search-google"
        if with_custom then values[#values+1]="custom" end
        return values
    end
    -- Mirrors default_buttons in activity.cpp, including older button settings.
    local function button_slots(opts)
        if type(opts.button_slots)=="table" then return opts.button_slots end
        if opts.button_slots==false then opts={} end
        if opts.buttons then
            local slots={}
            for _,b in ipairs(opts.buttons) do slots[#slots+1]={site="custom",label=b.label,url=b.url} end
            return slots
        end
        if opts.button==false then return {} end
        if layout=="recording" then return {{site="lastfm",fallback="provider"}} end
        return {{site="provider"}}
    end
    local function appearance_opts()
        local opts={}
        if not replace then for k,v in pairs(snap.appearance or {}) do opts[k]=v end end
        local saved=opts[layout.."_layout"] or {}
        if saved.preset then opts={} end
        for k,v in pairs(saved) do opts[k]=v end
        for k,v in pairs(draft) do opts[k]=v end
        return opts
    end
    -- Placeholders that the card can fill for each media layout (see format_activity in activity.cpp).
    local layout_fields={all={"title","artist","album","episode","original","group","channel","year","plays","loved","details","state"},
        anime={"title","episode","original","group","details","state"},audio={"title","artist","album","plays","loved","details","state"},
        recording={"title","artist","album","year","plays","loved","details","state"},video={"title","details","state"},
        movie={"title","year","details","state"},tv={"title","year","details","state"},stream={"title","channel","details","state"}}
    local field_help={title="Title of the anime, film, show, song or stream.",artist="Track artist.",album="Album or release.",
        episode="Episode number used for AniList.",original="Episode number from the filename.",group="Release group.",
        channel="Stream channel.",year="Release year.",details="The card's first text line before templates.",
        state="The card's second text line before templates.",status="Playback status, such as Playing or Paused (01:23).",
        plays="Your Last.fm play count for this track. Needs a connected Last.fm account.",loved="Shows Loved when you loved this track on Last.fm.",site="Name of the site the button links to."}
    -- Pages that edit or show the Appearance draft, so the preview includes unapplied changes.
    local draft_pages={Appearance=true,["Card preview"]=true,["Card buttons"]=true,["Button 1"]=true,["Button 2"]=true,
        ["Edit text"]=true,["Card text"]=true,["Artwork & badge"]=true,["Timer & pause"]=true,["Card links"]=true}
    local timer_names={progress="Progress",remaining="Remaining",session="Session",off="Off"}
    local text_target, unapplied, search_provider
    local search_names={anilist="AniList",["tmdb-movie"]="TMDB movies",["tmdb-tv"]="TMDB TV",tvmaze="TVmaze",musicbrainz="MusicBrainz"}
    local search_help={anilist="Anime titles. Choosing a result pins its AniList ID.",
        ["tmdb-movie"]="Films from TMDB.",["tmdb-tv"]="TV shows from TMDB. A result is saved for this file.",
        tvmaze="TV shows from TVmaze, including air-date episodes.",musicbrainz="Recordings from MusicBrainz, using the file's artist tag when present."}
    local large_image_names={cover="Cover or album art",still="Episode still",mpv="mpv logo",custom="Custom image",off="Off"}
    local large_image_help={cover="The matched anime, film, show or album artwork.",
        still="The TV episode's still image from TMDB or TVmaze. Other media use the cover.",
        mpv="The mpv logo, for a card that does not show artwork from the file.",
        custom="Your own public https image link. Placeholders are URL-encoded.",off="No large image."}
    local small_image_names={status="Play and pause",provider="Matched site",off="Off"}
    local small_image_help={status="A play or pause icon that follows playback.",
        provider="The icon of the site that matched this file, such as AniList or TMDB. Unmatched files show the play or pause icon.",
        off="No small badge or badge hover text."}
    local function unknown_fields(text,extra)
        local allowed={}
        for _,name in ipairs(layout_fields[layout] or layout_fields.all) do allowed[name]=true end
        for _,name in ipairs(extra or {}) do allowed[name]=true end
        local unknown={}
        for name in (text or ""):gmatch("{([%a_][%w_]*)}") do
            if not allowed[name] then unknown[#unknown+1]="{"..name.."}" end
        end
        return unknown
    end
    -- Opens the shared text editor with a placeholder list and a check before the value is kept.
    local function edit_text(target)
        text_target=target
        ui.push("Edit text")
    end
    local function button_summary(slot)
        if not slot or slot.site=="off" then return "Off" end
        local text=button_sites[slot.site] or slot.site
        if slot.fallback and slot.fallback~="off" then text=text.." → "..(button_sites[slot.fallback] or slot.fallback) end
        return text
    end
    local edit, edit_key, edit_base, list_status
    local status, status_timer, search
    local search_number=0
    local filter=""
    local pending_filter=""
    local last_read
    local record
    local saved_groups={
        {"Saved profiles",{"appearance","privacy","exclude","sources","tracking","scrobbling","panel","panel-theme","folder-filter","recognition-options","save-scopes","watch-retention"}},
        {"Saved matches",{"match","provider","tv-rule"}},
        {"Saved caches",{"cache"}},
        {"Saved AniList data",{"watch","pending","skipped","history","entry"}},
        {"Saved Last.fm data",{"lastfm-queue","lastfm-history"}},
    }
    local saved_categories={
        appearance={"Appearance profiles","Card appearance saved for all files, a folder, or a file. Removing a profile restores the inherited layout."},
        privacy={"Sharing profiles","Full, generic, or hidden Discord settings saved for a scope. Removing a restriction keeps currently open files protected."},
        exclude={"Exclusions","Folders or files blocked from sharing, lookup and tracking. Removing a rule keeps currently open files protected."},
        sources={"Recognition rules","Media type or provider choices saved for a file, folder or all files."},
        tracking={"Tracking preferences","AniList mode, threshold and Ask preferences saved for each scope. Watch history has its own category."},
        panel={"Panel settings","The saved panel position and optional Discord pin handoff. Clearing this record uses the default for each."},
        ["panel-theme"]={"Panel theme","The saved panel palette, opacity and blur. Clearing this record restores Dark glass."},
        ["save-scopes"]={"Saved scope selections","The Save for choice remembered separately by each settings page. Clearing it restores each page's initial choice."},
        ["watch-retention"]={"Watch retention settings","The number of months local watch records are kept without playback activity. Clearing it restores the three-month default."},
        ["folder-filter"]={"Folder filter override","The saved folder filter mode or list. Removing it restores both values from rich-presence.conf."},
        ["recognition-options"]={"Optional recognition settings","Clear the switches for conditional TV folder rules and local metadata files. Saved rules and the sidecar files stay available."},
        ["tv-rule"]={"TV folder rules","Saved series identities and approved filename titles for an exact folder. Removing a folder record removes its rules. File-specific matches stay available."},
        match={"AniList corrections","Pinned AniList IDs, episode offsets and non-anime flags. Open a record to remove individual fields."},
        provider={"Other provider matches","Pinned movie, TV and music metadata. Clearing a match returns the scope to automatic lookup or its chosen provider."},
        cache={"Metadata cache","Temporary provider responses. Clearing them makes the player fetch metadata again when needed."},
        watch={"Episode timers","Real playback time for each episode and viewing cycle. Clearing a timer also clears its pending and skipped updates."},
        pending={"Pending proposals","Eligible episode updates waiting for review. Clearing these removes local proposals."},
        skipped={"Skipped episodes","Local decisions to skip an episode's update. Clearing a skip lets the episode qualify again."},
        history={"Completed updates","Local records of progress already accounted for. Your remote AniList progress is stored separately."},
        scrobbling={"Scrobbling preferences","Last.fm scrobbling choices saved for all files, a folder or a file."},
        ["lastfm-queue"]={"Queued scrobbles","Scrobbles waiting to reach Last.fm, for example while offline. Clearing them means those plays are never sent."},
        ["lastfm-history"]={"Recent scrobbles","The local list of the last 20 scrobbles and their status. Your Last.fm profile is not changed."},
        entry={"Cached list entries","The last local copy of AniList progress and status. The player refreshes it from AniList when needed."},
    }
    local function category_for(page)
        for key,category in pairs(saved_categories) do
            if page=="Saved: "..category[1] then return key end
        end
    end
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
    local list_status_labels={CURRENT="Watching",PLANNING="Planning",PAUSED="Paused",DROPPED="Dropped",
        COMPLETED="Completed",REPEATING="Rewatching"}
    local tracking_mode_labels={off="Off",manual="Manual",ask="Ask",auto="Auto"}
    local ask_timing_labels={threshold="At threshold",["end"]="End of episode",leave="Before leaving"}
    local ask_positions={"top-right","top-center","top-left","bottom-right","bottom-center","bottom-left"}
    local leave_positions={"center","top-right","top-center","top-left","bottom-right","bottom-center","bottom-left"}
    local function copy(t) local r={}; for k,v in pairs(t or {}) do r[k]=v end; return r end
    local function theme_settings()
        local saved=snap.panel_theme or {}
        local preset=themes[saved.preset] and saved.preset or "glass"
        local config=copy(themes[preset])
        if options.panel_blur==false and not saved.preset then config.blur=false end
        for key,value in pairs(saved) do config[key]=value end
        return config
    end
    local cached_theme, theme_key
    local function panel_theme()
        local config=theme_settings()
        local key=table.concat({config.background,config.accent,config.opacity,tostring(config.blur),config.strength,config.method or "classic",tostring(config.contrast)},":")
        if key==theme_key then return cached_theme end
        local function bgr(rgb) return rgb:sub(5,6)..rgb:sub(3,4)..rgb:sub(1,2) end
        local function tint(first,second,fraction)
            local out={}
            for i=1,5,2 do
                local a=tonumber(first:sub(i,i+1),16)
                local b=tonumber(second:sub(i,i+1),16)
                out[#out+1]=string.format("%02X",math.floor(a+(b-a)*fraction+.5))
            end
            return bgr(table.concat(out))
        end
        local background,accent=config.background,config.accent
        local function light(rgb)
            return .2126*tonumber(rgb:sub(1,2),16)+.7152*tonumber(rgb:sub(3,4),16)+.0722*tonumber(rgb:sub(5,6),16)>145
        end
        local readable=light(background) and "100D09" or "F7F3EF"
        theme_key=key
        cached_theme={black=bgr(background),rail=bgr(background),row=tint(background,"FFFFFF",.08),
            focus=tint(background,accent,.31),hover=tint(background,accent,.18),
            text=readable,muted=light(background) and "35302C" or "C2B8AD",
            accent=bgr(accent),ink=light(accent) and "1B1008" or "F7F3EF",rim="ABA096",
            switch=tint(background,accent,.42),hint=tint(background,"202835",.65),
            opacity=config.opacity/100,blur=config.blur,strength=config.strength,method=config.method}
        if config.contrast then
            -- Pure white text, light secondary text and a strong focus fill for low vision.
            cached_theme.text,cached_theme.muted,cached_theme.rim="FFFFFF","EBEBEB","FFFFFF"
            cached_theme.row,cached_theme.hover,cached_theme.focus=bgr("1C1C1C"),tint(background,accent,.30),tint(background,accent,.48)
            cached_theme.ink="000000"
        end
        return cached_theme
    end
    local function request(action,fields)
        local r=fields or {}; r.action=action; r.session=snap.session; send(r)
    end
    local function edits()
        return layout=="all" and draft or {[layout.."_layout"]=draft}
    end
    local function refresh()
        local preview=draft_pages[ui.page]
        request("snapshot",{nonce="menu",page=ui.page,records=category_for(ui.page),
            replace=preview and replace and layout=="all",draft=preview and edits() or nil})
    end
    local function service(op,args)
        args=args or {}; args.op=op
        args.account=(snap.account or {}).generation
        args.notice_page=args.notice_page or ui.page
        request("service",{job=args})
    end
    local function start_search(provider,query,scope,identifier)
        search_number=search_number+1
        search={id=tostring(search_number),provider=provider,query=query,session=snap.session,scope=scope,identifier=identifier}
        service("search",{provider=provider,query=query,identifier=identifier,request=search.id,notice_page="Search results"})
        if ui.page~="Search results" then ui.push("Search results") else ui.draw() end
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
    local function scope_available(scope)
        return scope=="global" or (scope=="folder" and snap.has_local_folder)
            or (scope=="file" and snap.has_file)
    end
    local function get_scope(page)
        if page=="Search results" then return search and search.scope or "file" end
        return scopes[page] or (snap.save_scopes or {})[page]
            or (page=="Recognition & folders" and snap.has_file and "file" or "global")
    end
    local function require_scope(scope)
        if scope_available(scope) then return true end
        notice(scope=="folder" and "Open a local file to use the Folder scope." or "Open media to use the File scope.")
    end
    -- Folder previews count media below the folder once, up to a limit, and reuse the result.
    local folder_counts={}
    local media_extensions={}
    for ext in ("mkv mp4 avi webm mov m4v ts m2ts wmv flv mpg mpeg ogv flac mp3 m4a ogg opus wav aac wma alac ape wv"):gmatch("%S+") do media_extensions[ext]=true end
    local function media_files(folder)
        if folder_counts[folder] then return folder_counts[folder] end
        local utils=require("mp.utils")
        local count,pending=0,{{folder,0}}
        while #pending>0 and count<2000 do
            local entry=table.remove(pending)
            local dir,level=entry[1],entry[2]
            for _,name in ipairs(utils.readdir(dir,"files") or {}) do
                local ext=name:match("%.([^.]+)$")
                if ext and media_extensions[ext:lower()] then count=count+1 end
            end
            if level<4 then
                for _,name in ipairs(utils.readdir(dir,"dirs") or {}) do pending[#pending+1]={utils.join_path(dir,name),level+1} end
            end
        end
        folder_counts[folder]=count
        return count
    end
    local function pick(page) if page=="Appearance" then draft={}; replace=false end; ui.push(page) end
    local function rows(page)
        local list={}
        local scope=get_scope(page)
        local function apply(cat,values,target)
            target=target or scope
            if not require_scope(target) then return false end
            request("apply",{category=cat,values=values,scope=target,replace=cat=="appearance" and replace and layout=="all"})
            refresh()
            return true
        end
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
        local function choice(label,values,current,fn,display,names,available,descriptions)
            local tip=help(label,page,true,values,current)
            local entries={}
            for i,value in ipairs(values) do
                local option=tip.options[i]
                local name=(names or {})[value] or option.label
                local disabled=available and not available(value)
                local detail=(descriptions or {})[value] or option.detail
                if disabled then detail=detail..(value=="folder" and " Open a local file to use this scope." or " Open media to use this scope.") end
                entries[i]={label=name:sub(1,1):upper()..name:sub(2),value=value,selected=value==current,
                    detail=detail,disabled=disabled}
                option.label,option.detail=entries[i].label,detail
            end
            local item=row(label.."   "..(display or (names or {})[current] or tostring(current)),
                function() ui.choose(label,entries,fn) end)
            item.dropdown,item.tip=true,tip
            return item
        end
        local function switch(label,v,fn)
            local value=v and "On" or "Off"
            local item=row(label.."   "..value,fn)
            item.toggle=v
            item.tip=help(label,page,true,{"On","Off"},value)
        end
        local function scope_row()
            choice("Save for",{"global","folder","file"},scope,function(v)
                scopes[page]=v
                request("apply",{category="save-scopes",values={[page]=v},scope="global"})
                if v~=scope and page=="Appearance" then draft={}; replace=false end
                refresh()
            end, nil, {global="Global",folder="Folder",file="File"},scope_available)
        end
        local function toggle(label,key,default)
            local v=draft[key]
            local opts=snap.appearance or {}
            if v==nil and layout~="all" then v=(opts[layout.."_layout"] or {})[key] end
            if v==nil then v=opts[key] end
            if v==nil then v=default end
            switch(label,v,function() appearance_edit(key,not v); refresh() end)
        end
        local account=snap.account or {}
        local answer=snap.service or {}
        if (answer.context and answer.context~=snap.session) or (answer.generation and answer.generation~=account.generation) then answer={} end
        local entry=answer.entry
        local valid_entry=entry and entry.media==snap.match and entry.viewer==account.id and entry.session==snap.session
        -- Now playing and AniList share one progress draft and the same confirmation.
        local function bump(n) if not edit_base then edit_base=copy(entry) end; edit=math.max(0,(edit or entry.progress)+n); if snap.total>0 then edit=math.min(edit,snap.total) end end
        local function submit_progress(target)
            local message=account.name.." · "..snap.title.." · "..entry.progress.." → "..target
            if list_status then message=message.." · set status to "..list_status end
            if not entry.id then message="Create entry · "..message end
            if target>entry.progress+1 then message="Episode gap · "..message end
            local base=edit_base or copy(entry)
            confirm(message,function() service("write",{base=base,target=target,status=list_status}); list_status=nil; edit=nil; edit_base=nil end)
        end
        local function progress_row(page)
            local draft_row=row("Episodes watched   "..tostring(edit or entry.progress),nil,false,bump)
            draft_row.stepper,draft_row.disabled=true,false
            draft_row.tip=help(draft_row.label,page,true)
        end
        local card=snap.card or {}
        local tracking=snap.tracking or {}
        local function review_update(prop)
            local base=prop.base or {}
            local episode=tostring(prop.episode)
            local progress=tostring(prop.before).." → "..episode
            local action="Update episode "..episode
            if prop.repeat_count then action=prop.start_rewatch and "Record completed rewatch" or "Finish rewatch"
            elseif prop.start_rewatch then action="Start rewatch and record episode "..episode
            elseif prop.status=="REPEATING" then action="Resume rewatch and update"
            elseif prop.status=="CURRENT" then action=base.id and "Update and switch to Watching" or "Add to Watching and update" end
            local function submit(keep)
                local message=(account.name or "AniList").." · "..((prop.title and prop.title~="") and prop.title or snap.title or "Anime")
                    .." · "..progress
                if prop.status and not keep then
                    message=message.." · "..(list_status_labels[base.status] or "Not on your list")
                        .." → "..list_status_labels[prop.status]
                elseif keep then
                    message=message.." · Keep "..(list_status_labels[base.status] or base.status).." for later updates"
                end
                if prop.repeat_count then
                    message=message.." · Rewatches "..(base["repeat"] or 0).." → "..prop.repeat_count
                end
                if prop.start_rewatch and prop.episode<prop.before then message=message.." · Reset progress for a new rewatch"
                elseif prop.episode>prop.before+1 then message=message.." · Episode gap" end
                confirm(message,function() service("accept",{proposal=prop.key,keep_status=keep}) end)
            end
            row(action.."   "..progress,function() submit(false) end,true)
            if prop.status=="CURRENT" and base.id then
                row("Update progress only   "..progress,function() submit(true) end)
            end
        end
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
            row("Discord   "..(snap.reason or "Waiting for mpv"))
            row("Activity   "..(card.type or "").." "..(card.name or ""))
            if card.details and card.details~=card.name then row("Details   "..card.details) end
            if card.state and card.state~=card.name then row("State   "..card.state) end
            if snap.match and snap.match>0 and valid_entry and account.name then
                progress_row(page)
                if edit and edit~=entry.progress then
                    row("Apply progress",function() submit_progress(edit) end)
                elseif snap.watch_episode and tonumber(snap.episode) and tonumber(snap.episode)>entry.progress then
                    row("Mark current episode watched",function() submit_progress(tonumber(snap.episode)) end)
                end
            end
            local lastfm_now=snap.lastfm or {}
            local listen_now=lastfm_now.listen or {}
            if listen_now.track and (listen_now.identity or {})[1]==snap.session then
                local progress=listen_now.scrobbled and "Scrobbled" or listen_now.point and
                    (duration(listen_now.credited or 0).." of "..duration(listen_now.point)) or "Too short to scrobble"
                row("Last.fm   "..progress,function() pick("Last.fm") end,false,nil,"Last.fm status")
            end
            row("Why this match?",function() ui.push("Match evidence") end)
            if snap.fingerprint_available then
                row("Fingerprint this file",function() start_search("acoustid") end)
            end
            row("View outgoing card",function() draft={}; replace=false; ui.push("Card preview") end)
            row("Correct match",function() pick("Recognition & folders") end)
            if snap.allowed and (snap.match or 0)>0 then
                row("Correct episode",function()
                    local id, session, total=snap.match, snap.session, snap.total or 0
                    ui.text("Episode number on this AniList entry",snap.episode or "",function(s)
                        if session~=snap.session or id~=snap.match then return end
                        local number=tonumber(s)
                        if not number or number~=math.floor(number) or number<1 or number>9999 or
                            (total>0 and number>total) then
                            return notice("Enter a whole episode number within the AniList entry.")
                        end
                        confirm("Use AniList episode "..number.." for this file? Saved watch time stays with its original episode.",function()
                            if snap.match==id then apply("match",{id=id,episode=number,not_anime=false},"file") end
                        end)
                    end)
                end)
            end
            local lastfm_state=snap.lastfm or {}
            local listening=(lastfm_state.listen or {}).track
            if (lastfm_state.account or {}).name and listening and ((lastfm_state.listen or {}).identity or {})[1]==snap.session then
                local loved=(lastfm_state.track or {}).loved
                row(loved and "Remove love on Last.fm" or "Love on Last.fm",function()
                    service(loved and "lastfm-unlove" or "lastfm-love"); refresh()
                end,false,nil,"Love on Last.fm")
            end
            if (tonumber(snap.hidden_seconds) or 0)>0 then
                row("Resume Discord now",function() request("hide_minutes",{minutes=0}); refresh() end)
            elseif not snap.hidden_until_exit then
                row("Hide Discord for 1 hour",function() request("hide_minutes",{minutes=60}); refresh() end)
            end
            row(snap.hidden_file and "Share this file" or "Hide this file",function() request("hide_file"); refresh() end)
            row("Use this window for Discord",function() request("claim"); refresh() end)
            if snap.match and snap.match>0 then row(valid_entry and ("AniList progress   "..entry.progress) or "View AniList progress",function() pick("AniList") end) end
        elseif page=="Match evidence" then
            row("Recognition   "..(snap.recognition_status or "Waiting"))
            row("Provider   "..(snap.provider or "local"))
            row("Recognition rule   "..(snap.source_scope or "configuration"))
            row("Match rule   "..(snap.match_scope or "configuration"))
            if snap.episode_review and snap.episode_review~="" then row("Review reason   "..snap.episode_review) end
            local names={kind="Media kind",title="Title",year="Year",season="Season",episode="Episode",tmdb="TMDB ID",imdb="IMDb ID"}
            for _,key in ipairs({"kind","title","year","season","episode","tmdb","imdb"}) do
                local file=(snap.metadata_origin or {})[key]
                if file then row(names[key].."   from "..file,nil,false,nil,"Metadata "..key).tip="Read only. This field came from a local metadata file beside the media." end
            end
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
        elseif page=="Card buttons" then
            local slots=button_slots(appearance_opts())
            for i=1,2 do
                local n=i
                row("Button "..n.."   "..button_summary(slots[n]),function() ui.push("Button "..n) end)
            end
            row("Restore default buttons",function()
                appearance_edit("button_slots",false); refresh(); ui.back()
            end)
        elseif page=="Button 1" or page=="Button 2" then
            local n=tonumber(page:sub(-1))
            local slots=button_slots(appearance_opts())
            local slot=copy(slots[n] or {site="off"})
            local function store(changes)
                local list={copy(slots[1] or {site="off"}),copy(slots[2] or {site="off"})}
                for k,v in pairs(changes) do slot[k]=v end
                if slot.label=="" then slot.label=nil end
                if slot.site~="custom" then slot.url=nil end
                if slot.fallback=="off" then slot.fallback=nil end
                list[n]=slot
                if list[2].site=="off" then list[2]=nil end
                appearance_edit("button_slots",list)
                refresh()
            end
            choice("Site",button_values(true),slot.site or "off",function(v) store({site=v}) end,nil,button_sites,nil,button_site_help)
            if slot.site~="off" then
                local default=slot.site and slot.site:match("^search%-") and "Search on {site}" or "View on {site}"
                row("Label   "..(slot.label or default),function()
                    edit_text({label="Label",value=slot.label or default,limit=32,extra={"site"},
                        save=function(v) store({label=v~=default and v or ""}) end})
                end)
            end
            if slot.site=="custom" then
                row("Custom link   "..(slot.url or "Not set"),function()
                    edit_text({label="Custom link",value=slot.url or "",limit=512,save=function(v)
                        if not v:match("^https://[%w%-%.]+%.%a%a+") then
                            notice("Enter a public link that starts with https://."); return false
                        end
                        store({url=v})
                    end})
                end)
            elseif slot.site~="off" then
                local values=button_values(false)
                choice("Fallback",values,slot.fallback or "off",function(v) store({fallback=v}) end,nil,button_sites,nil,button_site_help)
            end
        elseif page=="Edit text" then
            local t=text_target
            if not t then row("Nothing to edit") else
                local value=t.value or ""
                row(t.label.."   "..(value~="" and value or "Empty"),function()
                    ui.text(t.label,value,function(v) t.value=v; ui.draw() end)
                end,false,nil,t.label)
                local fields=copy(layout_fields[layout] or layout_fields.all)
                for _,name in ipairs(t.extra or {}) do fields[#fields+1]=name end
                local names,descriptions={},{}
                for _,name in ipairs(fields) do names[name]="{"..name.."}"; descriptions[name]=field_help[name] end
                choice("Insert placeholder",fields,"",function(name) t.value=value.."{"..name.."}"; ui.draw() end,
                    "Choose",names,nil,descriptions)
                local unknown=unknown_fields(value,t.extra)
                row("Check   "..(#unknown==0 and "Ready" or ("Unknown "..table.concat(unknown,", "))),nil,false,nil,"Check")
                row("Discord limit   "..t.limit.." characters",nil,false,nil,"Discord limit")
                row("Keep",function()
                    if #unknown>0 then return notice("Remove "..table.concat(unknown,", ").." or choose a listed placeholder.") end
                    if t.save(value)==false then return end
                    text_target=nil; ui.back(); refresh()
                end,true)
                row("Cancel",function() text_target=nil; ui.back() end)
            end
        elseif page=="Appearance" then
            local opts=appearance_opts()
            row("Preview   "..(card.name or "mpv").." · "..(card.state or card.details or ""),function() ui.push("Card preview") end)
            scope_row()
            choice("Media layout",{"all","anime","audio","video","movie","tv","recording","stream"},layout,function(v)
                layout=v; draft={}; replace=false; refresh()
            end,nil,layout_names,nil,layout_help)
            choice("Preset",{"original","clean","detailed","private"},opts.preset or "original",function(v)
                replace=true; draft={preset=v}
                refresh()
            end,opts.custom and "Custom" or nil)
            local private=opts.preset=="private"
            local large=opts.large_image or ((opts.art==false or private) and "off" or "cover")
            local slots=button_slots(opts)
            row("Text   "..(opts.headline or opts.details_template or opts.state_template or "Preset layout"),
                function() ui.push("Card text") end,false,nil,"Text")
            row("Artwork & badge   "..large_image_names[large],function() ui.push("Artwork & badge") end,false,nil,"Artwork & badge")
            row("Buttons   "..button_summary(slots[1])..(slots[2] and (" · "..button_summary(slots[2])) or ""),
                function() ui.push("Card buttons") end,false,nil,"Buttons")
            row("Timer & pause   "..(timer_names[opts.timer or (private and "off" or "progress")] or opts.timer),
                function() ui.push("Timer & pause") end,false,nil,"Timer & pause")
            row("Links   "..((opts.large_url or opts.details_url or opts.state_url) and "Set" or "None"),
                function() ui.push("Card links") end,false,nil,"Links")
            row("Reset saved appearance",function()
                if not require_scope(scope) then return end
                confirm("Clear appearance overrides for "..scope.." and show the inherited card",function()
                    request("clear",{category="appearance",scope=scope}); draft={}; replace=false; refresh()
                end)
            end)
            row("Panel look",function() ui.push("Panel look") end)
            row("Apply",function()
                if apply("appearance",edits()) then draft={}; replace=false; ui.back() end
            end,true)
        elseif page=="Unapplied changes" then
            local leave=unapplied
            row("Your card edits for "..layout_names[layout].." are not saved.",nil,false,nil,"Unapplied summary")
            row("Apply and leave",function()
                local target=get_scope("Appearance")
                if not require_scope(target) then return end
                request("apply",{category="appearance",values=edits(),scope=target,replace=replace and layout=="all"})
                draft={}; replace=false; unapplied=nil
                ui.back(true); if leave then leave() end
            end,true)
            row("Discard changes",function()
                draft={}; replace=false; unapplied=nil
                ui.back(true); if leave then leave() end; refresh()
            end)
            row("Keep editing",function() unapplied=nil; ui.back(true) end)
        elseif page=="Last.fm" then
            local lastfm_state=snap.lastfm or {}
            local profile=lastfm_state.account or {}
            if profile.name then
                link("Account",profile.url or ("https://www.last.fm/user/"..profile.name),"Open the connected Last.fm profile.",profile.name)
                if profile.avatar and profile.avatar~="" then list[#list].avatar=profile.avatar end
                local item=row("Connection   "..(profile.expired and "Needs reconnecting" or profile.persistent and "Saved on this device" or "This session only"))
                item.tip="Read only. Saved connections use the operating system's credential store. Session-only connections end when mpv closes."
            else
                row("Account   Not connected")
            end
            if answer.lastfm_signin then
                row("Cancel browser sign-in",function() request("lastfm_cancel"); refresh() end,false,nil,"Cancel Last.fm sign-in")
                link("Approval page",answer.lastfm_signin,"Approve mpv-rich-presence on Last.fm if the browser did not open.","Open again")
            else
                row(profile.name and (profile.expired and "Reconnect Last.fm" or "Switch Last.fm account") or "Connect Last.fm",
                    function() service("lastfm-connect") end,not profile.name,nil,"Connect Last.fm")
                if not profile.name then
                    row("Connect for this session",function() service("lastfm-connect",{session_only=true}) end,false,nil,"Connect Last.fm for this session")
                end
            end
            if profile.name then row("Disconnect",function() ui.push("Disconnect Last.fm") end,false,nil,"Disconnect Last.fm") end
            local listen=lastfm_state.listen or {}
            if listen.track and (listen.identity or {})[1]==snap.session then
                row("Now listening   "..listen.track.artist.." – "..listen.track.track,nil,false,nil,"Now listening")
                local progress=listen.scrobbled and "Scrobbled" or listen.point and
                    (duration(listen.credited or 0).." of "..duration(listen.point)) or "Too short to scrobble"
                row("Scrobble progress   "..progress,nil,false,nil,"Scrobble progress")
                if profile.name then
                    local loved=(lastfm_state.track or {}).loved
                    row(loved and "Remove love on Last.fm" or "Love on Last.fm",function()
                        service(loved and "lastfm-unlove" or "lastfm-love"); refresh()
                    end,false,nil,"Love on Last.fm")
                end
            else
                row("Now listening   "..(snap.scrobble_allowed and "Waiting for an artist and title" or "Scrobbling is off here"),nil,false,nil,"Now listening")
            end
            local queued=tonumber(lastfm_state.queued) or 0
            row("Queued scrobbles   "..queued,profile.name and queued>0 and function() service("lastfm-send"); refresh() end or nil,
                false,nil,"Queued scrobbles")
            local status=lastfm_state.status or {}
            if status.error then row("Last problem   "..status.error,nil,false,nil,"Last problem") end
            row("Recent scrobbles",function() ui.push("Recent scrobbles") end)
            scope_row()
            local settings=snap.scrobbling or {}
            switch("Scrobbling",settings.enabled==true,function() apply("scrobbling",{enabled=not settings.enabled}) end)
            choice("Scrobble point",{"0","25","50","75","90"},tostring(settings.threshold or 0),function(v)
                apply("scrobbling",{threshold=tonumber(v)})
            end,nil,{["0"]="Last.fm default",["25"]="25% of the track",["50"]="Half the track",["75"]="75% of the track",["90"]="90% of the track"},nil,
                {["0"]="Half the track or four minutes, whichever comes first, as Last.fm recommends.",
                ["25"]="Count a play after a quarter of the track.",["50"]="Count a play after half of the track, however long it is.",
                ["75"]="Count a play after three quarters of the track.",["90"]="Count a play only when nearly the whole track was heard."})
            switch("Now Playing updates",settings.now_playing~=false,function() apply("scrobbling",{now_playing=settings.now_playing==false}) end)
            switch("Scrobble while hidden",settings.while_hidden==true,function() apply("scrobbling",{while_hidden=not settings.while_hidden}) end)
            switch("Scrobble music videos",settings.videos==true,function() apply("scrobbling",{videos=not settings.videos}) end)
        elseif page=="Recent scrobbles" then
            local tracks=((snap.lastfm or {}).history or {}).tracks or {}
            if #tracks==0 then row("No scrobbles yet",nil,false,nil,"No scrobbles yet") end
            for i,track in ipairs(tracks) do
                row(os.date("%d %b %H:%M",math.floor(track.timestamp)).."   "..track.artist.." – "..track.track.." · "..track.status,
                    nil,false,nil,"Recent scrobble "..i)
            end
        elseif page=="Disconnect Last.fm" then
            local name=((snap.lastfm or {}).account or {}).name or "Last.fm"
            row("Disconnect "..name.." and keep queued scrobbles",function()
                service("lastfm-disconnect"); ui.back(); refresh()
            end,false,nil,"Disconnect and keep queue")
            row("Disconnect and clear queued scrobbles",function()
                confirm("Disconnect "..name.." and clear queued and recent scrobbles on this device?",function()
                    service("lastfm-disconnect",{forget_queue=true}); refresh()
                end)
            end,false,nil,"Disconnect and clear queue")
            row("Cancel",ui.back)
        elseif page=="Card text" then
            local opts=appearance_opts()
            for _,item in ipairs({{"Title language","language",{"original","english","romaji","native"}},
                {"Episode numbering","numbering",{"relative","original","both"}}}) do
                local key=item[2]
                choice(item[1],item[3],opts[key] or item[3][1],function(v) appearance_edit(key,v); refresh() end)
            end
            for _,item in ipairs({{"Release group","group"},{"Episode","episode"},{"Details","details"},{"State","state"}}) do
                toggle(item[1],item[2],not (item[2]=="group" and opts.preset=="clean"))
            end
            choice("Compact status",{"name","details","state"},opts.compact or "name",function(v) appearance_edit("compact",v); refresh() end)
            toggle("Episode titles","spoilers",false)
            for _,item in ipairs({{"Headline","headline"},{"Details template","details_template"},{"State template","state_template"}}) do
                local label,key=item[1],item[2]
                row(label.."   "..(opts[key] or "Preset default"),function()
                    edit_text({label=label,value=opts[key] or "",limit=128,save=function(v) appearance_edit(key,v) end})
                end,false,nil,label)
            end
        elseif page=="Artwork & badge" then
            local opts=appearance_opts()
            local private=opts.preset=="private"
            local large=opts.large_image or ((opts.art==false or private) and "off" or "cover")
            choice("Large image",{"cover","still","mpv","custom","off"},large,function(v) appearance_edit("large_image",v); refresh() end,
                nil,large_image_names,nil,large_image_help)
            if large=="custom" then
                row("Large image link   "..(opts.large_image_url or "Not set"),function()
                    edit_text({label="Large image link",value=opts.large_image_url or "",limit=512,save=function(v)
                        if not v:match("^https://[%w%-%.]+%.%a%a+/") then
                            notice("Enter a public image link that starts with https://."); return false
                        end
                        appearance_edit("large_image_url",v)
                    end})
                end,false,nil,"Large image link")
            end
            local small=opts.small_image or ((opts.badge==false or private) and "off" or "status")
            choice("Small badge",{"status","provider","off"},small,function(v) appearance_edit("small_image",v); refresh() end,
                nil,small_image_names,nil,small_image_help)
            toggle("Tooltip","tooltip",true)
            for _,item in ipairs({{"Large hover text","large_text_template",{}},{"Badge hover text","small_text_template",{"status"}}}) do
                local label,key,extra=item[1],item[2],item[3]
                row(label.."   "..(opts[key] or "Default"),function()
                    edit_text({label=label,value=opts[key] or "",limit=128,extra=extra,save=function(v) appearance_edit(key,v) end})
                end,false,nil,label)
            end
        elseif page=="Timer & pause" then
            local opts=appearance_opts()
            choice("Timer",{"progress","remaining","session","off"},opts.timer or (opts.preset=="private" and "off" or "progress"),
                function(v) appearance_edit("timer",v); refresh() end)
            choice("While paused",{"keep","generic","hide"},opts.while_paused or "keep",function(v)
                appearance_edit("while_paused",v); refresh()
            end,nil,{keep="Keep the card",generic="Show a generic card",hide="Hide the card"},nil,
                {keep="Keep the full card until the pause timeout.",generic="Switch to a generic mpv card while paused. Playing again restores the full card.",
                hide="Hide the card as soon as playback pauses."})
            local conf_timeout=math.floor((tonumber(options.pause_timeout) or 1800)/60)
            row("Pause timeout   "..(opts.pause_timeout and (opts.pause_timeout==0 and "Never" or opts.pause_timeout.." min")
                or ("Conf default · "..conf_timeout.." min")),function()
                ui.text("Minutes before a paused card hides (0 never hides)",tostring(opts.pause_timeout or conf_timeout),function(v)
                    local minutes=tonumber(v)
                    if not minutes or minutes~=math.floor(minutes) or minutes<0 or minutes>1440 then
                        return notice("Enter whole minutes from 0 to 1440.")
                    end
                    appearance_edit("pause_timeout",minutes); refresh()
                end)
            end,false,nil,"Pause timeout")
        elseif page=="Card links" then
            local opts=appearance_opts()
            for _,item in ipairs({{"Artwork link","large_url"},{"Details link","details_url"},{"State link","state_url"}}) do
                local label,key=item[1],item[2]
                row(label.."   "..(opts[key] or "None"),function() ui.text(label,opts[key] or "",function(v) appearance_edit(key,v); refresh() end) end,false,nil,label)
            end
        elseif page=="Panel look" then
            local position=snap.panel_position
            if not position or position=="" then position="config" end
            choice("Panel position",panel_positions,position,function(v)
                if v=="config" then request("clear",{category="panel",scope="global",field="position"}); refresh()
                else apply("panel",{position=v},"global") end
            end,position=="config" and "Config · "..config_position or nil)
            local config=theme_settings()
            local function save(values,reset)
                request("apply",{category="panel-theme",values=values,replace=reset})
                refresh()
            end
            local function customize(key,value)
                config[key]=value
                config.preset="custom"
                save(config,true)
            end
            local preset=config.preset or "glass"
            choice("Panel theme preset",{"glass","midnight","graphite","contrast","custom"},preset,function(v)
                if v=="custom" then config.preset=v; save(config,true)
                else save({preset=v},true) end
            end,theme_names[preset],theme_names)
            for _,field in ipairs({{"Background color","background"},{"Accent color","accent"}}) do
                local name,key=field[1],field[2]
                row(name.."   #"..config[key],function()
                    ui.text(name.." (hex RGB)","#"..config[key],function(value)
                        local rgb=value:match("^#?(%x%x%x%x%x%x)$")
                        if not rgb then return notice("Enter six hex digits, for example #030405.") end
                        customize(key,rgb:upper())
                    end)
                end)
            end
            local function amount(name,key,low,high,suffix)
                row(name.."   "..config[key]..suffix,function()
                    ui.text(name.." ("..low.." to "..high..")",config[key],function(value)
                        local n=tonumber(value)
                        if not n or n~=math.floor(n) or n<low or n>high then
                            return notice("Enter a whole number from "..low.." to "..high..".")
                        end
                        customize(key,n)
                    end)
                end)
            end
            amount("Panel opacity","opacity",40,100,"%")
            switch("Background blur",config.blur,function() customize("blur",not config.blur) end)
            if config.blur then
                choice("Blur method",{"classic","gaussian","box"},config.method or "classic",
                    function(v) customize("method",v) end,nil,blur_names)
                amount("Blur strength","strength",2,32," px")
            end
            row("Restore panel theme defaults",function()
                request("clear",{category="panel-theme"}); refresh()
            end)
            row("Cancel",ui.back)
        elseif page=="Recognition & folders" then
            scope_row()
            local folder=(snap.folder_filter or {}).current
            if scope=="folder" and folder and folder~="" then
                local count=media_files(folder)
                row("Folder rule covers   "..(count>=2000 and "2000+" or tostring(count)).." media "..(count==1 and "file" or "files"),
                    nil,false,nil,"Folder rule covers").tip="Read only. Media files in "..folder.." and its subfolders that a folder rule here applies to, unless a file rule overrides it."
            end
            local current=(snap.sources or {}).source or "auto"
            local label=current
            for _,source in ipairs(sources) do if source[1]==current then label=source[2]; break end end
            local values,names={"inherit"},{inherit="Inherit recognition source"}
            for _,source in ipairs(sources) do values[#values+1]=source[1]; names[source[1]]=source[2] end
            choice("Media recognition",values,snap.source_scope=="configuration" and "inherit" or current,function(value)
                if not require_scope(scope) then return end
                if value=="inherit" then
                    confirm("Remove the recognition override for "..scope,function()
                        request("clear",{category="sources",scope=scope}); refresh()
                    end)
                else
                    confirm("Use "..names[value].." for "..scope,function() apply("sources",{source=value}) end)
                end
            end,snap.source_scope=="configuration" and ("Inherited · "..label) or label,names,nil,source_help)
            row("Source rule   "..(snap.source_scope or "configuration"))
            local recognition=snap.recognition_options or {}
            switch("TV folder rules",recognition.tv_rules==true,function()
                apply("recognition-options",{tv_rules=not recognition.tv_rules},"global")
            end)
            switch("Local metadata files",recognition.sidecar_metadata==true,function()
                apply("recognition-options",{sidecar_metadata=not recognition.sidecar_metadata},"global")
            end)
            switch("Ask before fingerprinting",recognition.confirm_fingerprint==true,function()
                apply("recognition-options",{confirm_fingerprint=not recognition.confirm_fingerprint},"global")
            end)
            row("Provider settings and credits",function() ui.push("Providers") end)
            -- One search form for every provider, prefilled with the current title.
            local provider=search_provider or (({["tmdb-movie"]="tmdb-movie",["tmdb-tv"]="tmdb-tv",tvmaze="tvmaze",
                musicbrainz="musicbrainz",acoustid="musicbrainz",wikidata="tmdb-movie"})[snap.provider or ""] or "anilist")
            choice("Search provider",{"anilist","tmdb-movie","tmdb-tv","tvmaze","musicbrainz"},provider,function(v)
                search_provider=v; ui.draw()
            end,nil,search_names,nil,search_help)
            local prefill=(snap.match or 0)>0 and snap.title or (card.name~="mpv" and card.name) or ""
            row("Search   "..(prefill~="" and prefill or "Enter a title"),function()
                ui.text("Search "..search_names[provider],prefill,function(q) start_search(provider,q,scope) end)
            end,true,nil,"Search")
            row("Enter ID or link",function() ui.text("AniList, TMDB or TVmaze ID or link","",function(s)
                s=s:match("^%s*(.-)%s*$")
                local id=tonumber(s:match("^https://anilist%.co/anime/(%d+)") or (provider=="anilist" and s:match("^%d+$")))
                if id then
                    return confirm("Use AniList "..id.." for "..scope,function() apply("match",{id=id,not_anime=false}) end)
                end
                local kind,number=s:match("^https://www%.themoviedb%.org/(%a+)/(%d+)")
                local target=kind=="movie" and "tmdb-movie" or kind=="tv" and "tmdb-tv" or nil
                if not target then
                    number=s:match("^https://www%.tvmaze%.com/shows/(%d+)")
                    target=number and "tvmaze" or nil
                end
                if not target and s:match("^%d+$") and provider~="anilist" and provider~="musicbrainz" then target,number=provider,s end
                if not target then return notice("Enter an AniList, TMDB or TVmaze ID or link.") end
                start_search(target,"",scope,tonumber(number))
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
            row("Fingerprint this file",function()
                start_search("acoustid",nil,scope)
            end)
        elseif page=="Providers" then
            link("AniList","https://anilist.co/","Supplies public anime titles, artwork and sequel links. Sign-in is only needed for list features.","Anime metadata")
            link("TMDB","https://www.themoviedb.org/","Supplies movie and TV titles, episode metadata and posters. The packaged default uses the shared service.",
                options.tmdb_key~="" and "Personal token" or "Shared service")
            row("This product uses the TMDB API but is not endorsed or certified by TMDB.",nil,false,nil,"TMDB attribution")
            link("TVmaze","https://www.tvmaze.com/","Supplies aired episode order and artwork as a TV fallback. Its data is licensed under CC BY-SA.","TV metadata · CC BY-SA")
            link("Wikidata","https://www.wikidata.org/","Supplies film titles and release years if TMDB cannot resolve a movie.","Movie fallback")
            link("MusicBrainz","https://musicbrainz.org/","Matches recording and album tags. The plugin checks artist credits and runtime before selecting a recording.","Music metadata")
            link("Cover Art Archive","https://coverartarchive.org/","Supplies artwork for identified MusicBrainz releases and release groups.","Album artwork")
            link("Last.fm","https://www.last.fm/","Receives scrobbles, Now Playing updates and loves after you connect an account. Recognition does not use Last.fm.","Scrobbling")
            link("AcoustID","https://acoustid.org/","Identifies local audio using a fingerprint. Only the fingerprint and duration are sent. The package includes an application key.",
                options.bundled_fpcalc and "Fingerprinting included" or "Uses fpcalc on PATH")
        elseif page=="Search results" then
            local result=answer.search or {}
            if not search or search.session~=snap.session then
                row("Playback changed. Start a new search.")
            elseif result.id~=search.id then row("Searching…")
            elseif result.state=="failed" then
                row("Search failed   "..(result.error or "Provider unavailable"))
                row("Retry search",function() start_search(search.provider,search.query,search.scope,search.identifier) end)
            else
                local current=search
                local tv=current.provider=="tmdb-tv" or current.provider=="tvmaze"
                for _,r in ipairs(result.results or {}) do
                    local hit=r
                    row(hit.title,function()
                        local target=tv and "file" or scope
                        if not require_scope(target) then return end
                        confirm("Use "..hit.title.." for "..target,function()
                            if search~=current then return end
                            service("select",{candidate=hit,scope=target,request=current.id})
                        end)
                    end)
                    if tv and snap.has_local_folder and (snap.recognition_options or {}).tv_rules then
                        local function save_rule(dedicated)
                            local prompt=dedicated and ("Does this folder contain only "..hit.title.."? Files with only an episode number can use this series.")
                                or ("Save a folder rule for "..hit.title.."?")
                            confirm(prompt,function()
                                if search~=current then return end
                                service("select",{candidate=hit,scope=dedicated and "tv-dedicated" or "tv-rule",request=current.id})
                            end)
                        end
                        local item=row("Use "..hit.title.." for matching files in this folder",function() save_rule(false) end)
                        item.tip="Save this series for files in this exact folder that share the approved series title. Each file still needs its own episode number or air date. Unclear or conflicting titles use Automatic recognition. Other shows can have their own rules."
                        item=row("Dedicated folder for "..hit.title,function() save_rule(true) end)
                        item.tip="Use only for a folder dedicated to this one series. A file named S02E04 can use this rule. A file with a conflicting title or identifier uses regular recognition. Only one series per folder can claim files with no title."
                    end
                end
                if #(result.results or {})==0 then row("No results") end
            end
        elseif page=="Privacy & windows" then
            local status_row=row("Discord status   "..(snap.reason or "Waiting"))
            status_row.tip="Read only. Shows whether this window can publish a full or generic card, or why sharing is paused or hidden. Other windows may publish their own cards."
            local lookup_row=row("Online metadata   "..(snap.allowed and "Allowed" or "Blocked"))
            lookup_row.tip="Read only. When allowed, supported providers receive the search title and relevant tags or episode numbers. When blocked, new lookups and AniList requests for this file are stopped. Private sessions and folder exclusions take priority."
            local tracking_row=row("AniList tracking   "..(snap.track_allowed and "Eligible" or "Off or blocked"))
            tracking_row.tip="Read only. Shows whether current permissions allow counting watch time. The AniList page also shows whether this particular episode is confirmed and can update the list."
            row("Local watch history",function() ui.push("Saved AniList data") end)
            switch("Release idle Discord pin",snap.yield_idle_pin==true,function()
                request("idle_pin",{enabled=not snap.yield_idle_pin}); refresh()
            end)
            scope_row()
            row("Folder filters",function() ui.push("Folder filters") end)
            choice("Discord",{"full","generic","hidden"},(snap.privacy or {}).mode or "full",function(v)
                confirm("Set "..scope.." sharing to "..v,function() apply("privacy",{mode=v}) end)
            end)
            row(snap.private and "End private session" or "Start private session",function()
                request("private",{enabled=not snap.private}); refresh()
            end)
            row("Exclude this folder",function() confirm("Block lookups, tracking and Discord for this folder?",function()
                apply("exclude",{blocked=true},"folder")
            end) end)
            -- Timed hiding shows when it ends, with one action to resume early.
            local left=tonumber(snap.hidden_seconds) or 0
            if left>0 or snap.hidden_until_exit then
                row("Hidden until   "..(snap.hidden_until_exit and "This window closes" or os.date("%H:%M",os.time()+left)),nil,false,nil,"Hidden until")
                row("Resume Discord now",function()
                    if left>0 then request("hide_minutes",{minutes=0}) end
                    if snap.hidden_until_exit then request("hide_until_exit",{enabled=false}) end
                    refresh()
                end)
            end
            choice("Hide Discord",{"15","60","180","window","custom"},"",function(v)
                if v=="window" then request("hide_until_exit",{enabled=true}); refresh()
                elseif v=="custom" then
                    ui.text("Minutes to hide (1 to 1440)","60",function(value)
                        local minutes=tonumber(value)
                        if not minutes or minutes~=math.floor(minutes) or minutes<1 or minutes>1440 then
                            return notice("Enter whole minutes from 1 to 1440.")
                        end
                        request("hide_minutes",{minutes=minutes}); refresh()
                    end)
                else request("hide_minutes",{minutes=tonumber(v)}); refresh() end
            end,"Choose",{["15"]="15 minutes",["60"]="1 hour",["180"]="3 hours",window="Until this window closes",custom="Custom…"},nil,
                {["15"]="Hide the card for 15 minutes in this window.",["60"]="Hide the card for an hour in this window.",
                ["180"]="Hide the card for three hours in this window.",window="Hide the card until this mpv window closes.",
                custom="Enter any number of minutes up to a day."})
            row(snap.pinned and "Return to last-resumed window" or "Pin this window for Discord",function()
                request(snap.pinned and "unpin" or "pin"); refresh()
            end)
            switch("Track while hidden",tracking.without_sharing==true,function()
                apply("tracking",{without_sharing=not tracking.without_sharing})
            end)
        elseif page=="Folder filters" then
            local settings=snap.folder_filter or {}
            local folders=settings.folders or {}
            local modes={disabled="Disabled",whitelist="Whitelist",blacklist="Blacklist"}
            local function update(fields) request("folder_filter",fields); refresh() end
            local function set_folders(paths) update({folders=paths}) end
            local function add(path)
                path=path:match("^%s*(.-)%s*$"):gsub('^"(.*)"$',"%1")
                if not (path:match("^%a:[/\\]") or path:match("^%a:$") or path:match("^[/\\][/\\]") or path:sub(1,1)=="/") then
                    notice("Enter an absolute folder path."); return
                end
                local paths=copy(folders)
                paths[#paths+1]=path
                set_folders(paths)
            end
            local mode=settings.mode or "disabled"
            row("Filter source   "..(settings.inherited==false and "Saved in panel" or "Configuration"))
            choice("Filter mode",{"disabled","whitelist","blacklist"},mode,function(v)
                confirm("Switch the folder filter to "..modes[v].." for all mpv windows?",function() update({mode=v}) end)
            end,modes[mode])
            local exception=settings.exception or "off"
            local access=exception~="off" and "Temporary exception" or settings.passes and "Allowed by filter" or "Blocked by filter"
            if not settings.current or settings.current=="" then access="Open a local file" end
            row("Current folder access   "..access)
            if settings.current and settings.current~="" then
                row("Current folder   "..settings.current)
                if exception~="off" then
                    row("End temporary exception",function() request("folder_exception",{mode="off"}); refresh() end)
                elseif not settings.passes then
                    row("Allow this file until it closes",function() request("folder_exception",{mode="file"}); refresh() end)
                    row("Bypass the filter in this window",function()
                        confirm("Allow local files outside the folder filter until this mpv window closes?",function()
                            request("folder_exception",{mode="window"}); refresh()
                        end)
                    end)
                end
                local listed=false
                for _,path in ipairs(folders) do if path==settings.current then listed=true; break end end
                if not listed then
                    row("Add current folder",function()
                        confirm("Add "..settings.current.." to the "..(mode=="blacklist" and "block list" or "folder list").."?",function() add(settings.current) end)
                    end)
                end
            end
            row("Listed folders   "..#folders,function() ui.push("Listed folders") end)
            row("Add folder path",function() ui.text("Absolute folder path","",add) end)
            if settings.inherited==false then
                row("Restore conf filter",function()
                    confirm("Restore the folder filter mode and list from rich-presence.conf?",function() update({mode="config"}) end)
                end)
            end
        elseif page=="Listed folders" then
            local folders=(snap.folder_filter or {}).folders or {}
            if #folders==0 then row("No folders listed") end
            for i,path in ipairs(folders) do
                local entry=row("Folder "..i.."   "..path,function()
                    confirm("Remove "..path.." from the folder filter list?",function()
                        local paths={}
                        for _,folder in ipairs(folders) do if folder~=path then paths[#paths+1]=folder end end
                        request("folder_filter",{folders=paths}); refresh()
                    end)
                end)
                entry.tip="Remove this directory from the list after reviewing the full path. In Whitelist mode this removes access. In Blacklist mode it removes a block."
            end
        elseif page=="AniList" then
            if account.name and tonumber(account.id) then
                local username=account.name:match("^[A-Za-z0-9_-]+$") and account.name or tostring(account.id)
                link("Account","https://anilist.co/user/"..username,
                    "Open the connected account's AniList profile.",account.name)
                list[#list].avatar=account.avatar and account.avatar.medium
            else
                row("Account   Not signed in")
            end
            if account.name then
                local item=row("Connection   "..(account.persistent and "Saved on this device" or "This session only"))
                item.tip="Read only. Saved connections use the operating system's credential store. Session-only connections expire when the helper or mpv exits."
            end
            row(account.expired and "Reconnect to AniList" or account.name and "Switch or reconnect account" or "Connect account",function() service("connect") end)
            if answer.signin then
                local item=row("Cancel browser sign-in",function() request("cancel_connect",{id=answer.signin}) end)
                item.tip="Cancel this browser attempt and discard the pending connection. A previously connected account stays active."
            end
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
            local elapsed=watch.elapsed or 0
            local mode=tracking.mode or "off"
            local watch_status
            if not account.id then watch_status="Connect AniList"
            elseif mode=="off" then watch_status="Tracking is off"..(elapsed>0 and (" · "..duration(elapsed).." saved") or "")
            elseif not snap.allowed then watch_status="Tracking blocked"
            elseif (snap.match or 0)==0 then watch_status="No anime match"
            elseif snap.episode_review and snap.episode_review~="" then watch_status="Episode needs review"
            elseif not snap.episode or snap.episode=="" then watch_status="No episode number"
            elseif not snap.watch_episode then watch_status="Episode needs review"
            elseif elapsed<=0 then watch_status="No time recorded yet"
            else watch_status=duration(elapsed) end
            row("Episode watch time   "..watch_status)
            local counting=({this="This window",other="Another mpv window",none="Nothing playing"})[watch.counting or ""]
            if counting then
                row("Counting watch time   "..counting,nil,false,nil,"Counting watch time")
                row("Publishing to Discord   "..(snap.owner and "This window" or "Another window or none"),nil,false,nil,"Publishing to Discord")
            end
            if tracking.coverage then
                local item=row("Unique footage watched   "..duration(watch.coverage or 0))
                item.tip="Read only. Shows how much distinct footage was played while this option was enabled. Replaying a scene counts once here. The episode watch timer and AniList threshold still use real elapsed playback time."
            end
            if snap.episode_review and snap.episode_review~="" then
                row("Episode type   "..snap.episode_review)
            end
            if elapsed>0 then
                row("Watch started   "..clock_time(watch.first_watched))
                row("Last watched   "..clock_time(watch.last_watched))
            end
            if answer.read_state=="loading" then row("List status   Loading…") end
            if answer.read_state=="offline" and not valid_entry then row("List status   Offline · no confirmed progress") end
            if valid_entry then
                row(entry.id and ("List entry   "..entry.progress.." / "..(snap.total>0 and snap.total or "?")..
                    " · "..(list_status_labels[entry.status] or entry.status or "?")..(answer.offline and " · Offline" or "")) or "List status   Not on your list")
                progress_row(page)
                local submit=submit_progress
                choice("Status on Apply",{"unchanged","CURRENT","COMPLETED","REPEATING","PAUSED","DROPPED","PLANNING"},
                    list_status or "unchanged",function(v) list_status=v~="unchanged" and v or nil end,
                    nil,list_status_labels)
                row("Apply progress",function() submit(edit or entry.progress) end,true)
                row("Reset progress edit",function() edit=entry.progress; edit_base=copy(entry) end)
                if snap.watch_episode and tonumber(snap.episode) then row("Mark current episode watched",function() submit(tonumber(snap.episode)) end) end
            end
            scope_row()
            choice("Tracking",{"off","manual","ask","auto"},mode,function(v) apply("tracking",{mode=v}) end,
                nil,tracking_mode_labels)
            if mode=="ask" then
                local timing=tracking.ask_timing or "threshold"
                choice("Ask timing",{"threshold","end","leave"},timing,function(v)
                    apply("tracking",{ask_timing=v})
                end,ask_timing_labels[timing])
                choice("Ask prompt position",ask_positions,
                    tracking.ask_position or "top-right",function(v) apply("tracking",{ask_position=v}) end)
            end
            if mode=="ask" or mode=="auto" then
                local position=tracking.ask_leave_position or "center"
                choice("Before leaving position",leave_positions,position,
                    function(v) apply("tracking",{ask_leave_position=v}) end,position=="center" and "Center" or position)
                local seconds=tracking.ask_timeout or 30
                local names={never="Never",["15"]="15 seconds",["30"]="30 seconds",["60"]="60 seconds",custom="Custom timeout"}
                local current=seconds==0 and "never" or tostring(seconds)
                choice("Ask prompt timeout",{"never","15","30","60","custom"},names[current] and current or "custom",function(v)
                    if v~="custom" then apply("tracking",{ask_timeout=tonumber(v) or 0}); return end
                    ui.text("Dismiss after 5 to 300 seconds",tostring(seconds>0 and seconds or 30),function(s)
                        local value=tonumber(s)
                        if value and value==math.floor(value) and value>=5 and value<=300 then
                            apply("tracking",{ask_timeout=value})
                        else notice("Enter a whole number from 5 to 300 seconds.") end
                    end)
                end,seconds==0 and "Never" or seconds.." seconds",names)
            end
            switch("Resume unfinished time",tracking.resume~=false,function()
                apply("tracking",{resume=tracking.resume==false})
            end)
            switch("Measure unique footage",tracking.coverage==true,function()
                apply("tracking",{coverage=not tracking.coverage})
            end)
            switch("Keep offline proposals",tracking.keep_pending==true,function()
                apply("tracking",{keep_pending=not tracking.keep_pending})
            end)
            row("Pending updates   "..#(snap.pending or {}),function() ui.push("Pending updates") end)
            switch("Rewatch tracking",tracking.rewatch==true,function() apply("tracking",{rewatch=not tracking.rewatch}) end)
            if tracking.rewatch then
                row("Start new viewing cycle",function()
                    confirm("Start a new local viewing cycle for "..(snap.title or "this anime")..
                        "? Watch counters start fresh and pending updates for this entry are cleared.",function() service("rearm") end)
                end)
            end
            local threshold=tracking.threshold or 15
            row("Threshold   "..threshold.." minutes",function()
                ui.text("Minimum elapsed minutes",tostring(threshold),function(s)
                    local value=tonumber(s)
                    if value and value==math.floor(value) and value>=1 and value<=1440 then apply("tracking",{threshold=value})
                    else notice("Enter a whole number from 1 to 1440 minutes.") end
                end)
            end)
            if answer.proposal and snap.allowed and snap.watch_episode and answer.proposal.media==snap.match
                and answer.proposal.episode==tonumber(snap.episode) then
                local p=answer.proposal
                review_update(p)
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
            if #(snap.pending or {})>0 then
                local item=row("Filter updates",function() ui.text("Filter by title or episode",pending_filter,function(value) pending_filter=value end) end)
                item.tip="Search pending proposals by anime title or episode number. Filtering changes the display only. Open the matching episode to verify and apply an update."
                if pending_filter~="" then row("Clear filter",function() pending_filter="" end) end
            end
            local visible=0
            for _,item in ipairs(snap.pending or {}) do
                local prop=item
                local label=(prop.title~="" and prop.title or ("AniList "..prop.media)).." · Episode "..prop.episode
                if label:lower():find(pending_filter:lower(),1,true) then
                    visible=visible+1
                    -- One label per update says what it is waiting for.
                    local current=snap.allowed and snap.watch_episode and prop.media==snap.match and prop.episode==tonumber(snap.episode)
                    local listed=valid_entry and prop.media==snap.match and entry.id and (prop.base or {}).progress~=nil
                    local state=prop.offline and "Needs a refresh"
                        or (listed and entry.progress~=prop.base.progress) and "Conflict"
                        or prop.needs_review and not current and "Unconfirmed"
                        or current and "Ready" or "Play to apply"
                    row(label.."   "..state,nil,false,nil,"Proposal "..prop.key).tip=({
                        ["Needs a refresh"]="AniList could not be read when this update was proposed. Play the episode or select Check now to read your list again.",
                        Conflict="Your AniList progress changed after this update was proposed. Review it against the current list before applying.",
                        Unconfirmed="An earlier attempt to send this update could not be confirmed. Play the episode to review it against your list.",
                        Ready="The episode is playing. Review and apply the update below.",
                        ["Play to apply"]="Play this episode to review and apply its update."})[state]
                    if prop.media==snap.match and snap.allowed and (prop.offline or state=="Conflict") then
                        row("Check now",function() service("read"); refresh() end,false,nil,"Check pending "..prop.key)
                    end
                    if current and not prop.offline then review_update(prop) end
                    row("Skip "..label,function() confirm("Skip "..label,function() service("skip",{proposal=prop.key}) end) end)
                end
            end
            if #(snap.pending or {})==0 then row("No pending updates") end
            if #(snap.pending or {})>0 and visible==0 then row("No updates match the filter") end
        elseif page=="Saved data" then
            local counts=snap.record_counts or {}
            for _,group in ipairs(saved_groups) do
                local name,count=group[1],0
                for _,category in ipairs(group[2]) do count=count+(counts[category] or 0) end
                row(name.."   "..count,function() filter=""; ui.push(name) end)
            end
            row("Settings transfer",function() ui.push("Settings transfer") end)
            local months=(snap.watch_retention or {}).months or 3
            row("Watch data retention   "..(months==0 and "Never expire" or months..(months==1 and " month" or " months")),function()
                ui.text("Keep local watch data for months (0 for no expiry)",tostring(months),function(value)
                    local n=tonumber(value)
                    if not n or n~=math.floor(n) or n<0 or n>120 then return notice("Enter 0 to 120 months.") end
                    apply("watch-retention",{months=n},"global")
                    service("sweep-watch")
                end)
            end)
            row("Reset all watch data",function()
                confirm("Clear local watch timers, viewing cycles, progress history and pending updates for every account on this install? Saved settings and your AniList list entries stay as they are.",function()
                    service("reset-watch",{notice_page="Saved data"})
                end)
            end)
            row("Reset everything",function() confirm("Clear local profiles, matches, caches and watch history, disconnect AniList, and restore the defaults from rich-presence.conf.",function()
                scopes={}; service("reset")
            end) end)
        elseif page=="Settings transfer" then
            local function transfer(op,prompt)
                ui.text(prompt,options.settings_export_path or "",function(path)
                    if path=="" then return notice("Enter an absolute path to a JSON file.") end
                    if op=="export-settings" then service(op,{path=path})
                    else confirm("Import settings from "..path.."? Matching saved profiles and rules will be replaced.",
                        function() scopes={}; service(op,{path=path,notice_page="Settings transfer"}) end) end
                end)
            end
            row("Export settings",function() transfer("export-settings","Save settings to a new JSON file") end)
            row("Import settings",function() transfer("import-settings","Open a settings JSON file") end)
        elseif category_for(page) then
            local cat=category_for(page)
            local label,description=saved_categories[cat][1],saved_categories[cat][2]
            local count=(snap.record_counts or {})[cat] or 0
            local shown=0
            local function clear(scope)
                local suffix=scope and (" ("..scope..")") or " (all "..count..")"
                confirm("Clear "..label:lower()..suffix.."? "..description,function()
                    if cat=="save-scopes" then scopes={} end
                    service("clear",{category=cat,scope=scope,notice_page=page})
                end)
            end
            if count>0 then
                row("Filter records"..(filter~="" and "   "..filter or ""),function()
                    ui.text("Filter this category",filter,function(v) filter=v end)
                end)
                if filter~="" then row("Clear search",function() filter="" end) end
                if cat=="cache" or cat=="watch" or cat=="pending" or cat=="skipped" or cat=="history" or cat=="entry" or cat=="lastfm-queue" then
                    row("Clear all   "..count,function() clear() end).tip=description.." This applies to every record in this category."
                end
            end
            for _,item in ipairs(snap.records or {}) do
                if item.category==cat and ((item.label or "").." "..item.scope):lower():find(filter:lower(),1,true) then
                    shown=shown+1
                    local title,value=label,item.scope
                    if cat=="cache" then
                        local kind=item.label or ""
                        title=kind:gsub("^v%d+:",""):gsub("^music%-v%d+:","Music · "):gsub(":"," · ")
                        if title=="" then title="Cached result" end
                        value=item.expires and item.expires>0 and
                            (item.expires<os.time() and "Expired" or "Expires "..os.date("%d %b",math.floor(item.expires))) or "Cached"
                    elseif cat=="lastfm-queue" then
                        title=(item.artist or "?").." – "..(item.track or "?")
                        value=item.timestamp and item.timestamp>0 and os.date("%d %b, %H:%M",math.floor(item.timestamp)) or "Queued"
                    elseif cat=="lastfm-history" then
                        title,value="Recent scrobbles",tostring(item.tracks or 0)..((item.tracks==1) and " play" or " plays")
                    elseif cat=="watch" or cat=="history" or cat=="pending" or cat=="skipped" or cat=="entry" then
                        local _,media,episode,cycle=item.scope:match("^(%d+)/(%d+)/(%d+)/(%d+)$")
                        if media then title,value="AniList "..media,"Episode "..episode.." · Cycle "..(tonumber(cycle)+1)
                        else
                            media=item.scope:match("^%d+/(%d+)$")
                            if media then title,value="AniList "..media,label end
                        end
                    end
                    row(title.."   "..value,function()
                        if cat=="match" then record=item; ui.push("Saved match")
                        else clear(item.scope) end
                    end,false,nil,cat..":"..item.scope).tip=description.." Record key "..item.scope
                end
            end
            if shown==0 then row(count==0 and "No saved records" or filter=="" and "Loading records…" or "No matching records") end
        elseif page=="Saved profiles" or page=="Saved matches" or page=="Saved caches" or page=="Saved AniList data"
            or page=="Saved Last.fm data" then
            local counts=snap.record_counts or {}
            for _,group in ipairs(saved_groups) do
                if group[1]==page then
                    for _,key in ipairs(group[2]) do
                        local name,description=saved_categories[key][1],saved_categories[key][2]
                        row(name.."   "..(counts[key] or 0),function() filter=""; ui.push("Saved: "..name) end).tip=description
                    end
                    break
                end
            end
        elseif page=="Saved match" and record then
            row("Scope   "..record.scope)
            for _,field in ipairs({{"id","Pinned AniList ID"},{"not_anime","Non-anime flag"},{"offset","Episode offset"},{"episode","Corrected episode"}}) do
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
            local build=options.build_info or {}
            local release="v"..(build.version or snap.version or "dev")
            if build.commit then release=release.." · "..tostring(build.commit):sub(1,7) else release=release.." · source build" end
            if build.platform then release=release.." · "..build.platform.." "..(build.architecture or "") end
            row("Release   "..release,nil,false,nil,"Release")
            row("Sharing   "..(snap.reason or "Waiting")); row("SDK   "..(snap.sdk or "Waiting"))
            if (snap.last_published or 0)>0 then row("Last Discord publish   "..clock_time(snap.last_published)) end
            row("Configuration   "..(snap.configured and "Received" or "Waiting"))
            if options.helper_status then row("Python helper   "..options.helper_status()) end
            row("Publisher   "..(snap.owner and "This window" or "Other window"))
            row("Lookup   "..(snap.allowed and "Allowed" or "Blocked"))
            row("Recognition status   "..tostring(snap.recognition_status or "Waiting"))
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
        avatar=options.avatar,backdrop=options.backdrop,theme=panel_theme,
        animate=options.menu_animations,help=help,changed=clear_notice,position=function()
            return snap.panel_position and snap.panel_position~="" and snap.panel_position or config_position
        end,
        leaving=function(page) if page=="Appearance" then draft={}; replace=false end end,
        before_leave=function(page,leave)
            if page~="Appearance" or (next(draft)==nil and not replace) then return true end
            unapplied=leave
            ui.push("Unapplied changes")
            return false
        end,
        closed=function() draft={}; edit=nil; edit_base=nil; list_status=nil; pending=nil; clear_notice(); if ui.terminal then mp.set_property("term-status-msg",ui.old_term or "") end end})
    ui.update=function(value)
        if not ui.open then return end
        local key=tostring(value.session).."/"..tostring(value.match).."/"..tostring((value.account or {}).generation)
        if key~=edit_key then edit=nil; edit_base=nil; list_status=nil; pending=nil; draft={}; edit_key=key end
        if (snap.session~=value.session or (snap.account or {}).generation~=(value.account or {}).generation)
            and ui.dismiss_choices then ui.dismiss_choices(false) end
        snap=value
        for page,scope in pairs(scopes) do
            if (snap.save_scopes or {})[page]==scope then scopes[page]=nil end
        end
        if (ui.page=="AniList" or ui.page=="Now playing") and value.allowed and (value.match or 0)>0 and (value.account or {}).id and last_read~=key then
            last_read=key; service("read",{quiet=true})
        end
        ui.draw()
    end
    return ui
end
