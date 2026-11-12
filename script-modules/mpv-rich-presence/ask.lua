-- SPDX-License-Identifier: AGPL-3.0-or-later
return function(make_sheet, send, options)
    local popup, prompt, deadline, busy, notice
    local statuses={CURRENT="Watching",PLANNING="Planning",PAUSED="Paused",
        DROPPED="Dropped",COMPLETED="Completed",REPEATING="Rewatching"}

    local function submit(op, keep)
        if busy or not prompt then return end
        busy=true
        notice=nil
        deadline=mp.get_time()+25
        local proposal=prompt.proposal
        send({action="service",session=prompt.session,ask_prompt=prompt.id,
            job={op=op,account=proposal.account,proposal=proposal.key,keep_status=keep,
                notice_page="AniList update"}})
        popup.draw()
    end

    popup=make_sheet({
        compact=true,sections={},scale=options.menu_scale,animate=options.menu_animations,
        position=function() return prompt and prompt.position or "top-right" end,
        refresh=function() popup.draw() end,
        footer=function()
            return busy and "Later · Saving…" or "Later · "..math.max(0,math.ceil(deadline-mp.get_time())).." s"
        end,
        closed=function()
            local previous=prompt
            prompt,notice,busy=nil,nil,false
            if popup.terminal then mp.set_property("term-status-msg",popup.old_term or "") end
            if previous then send({action="ask_dismiss",session=previous.session,id=previous.id}) end
        end,
        rows=function()
            local p=prompt.proposal
            local base=p.base or {}
            local rows={}
            local function row(id,label,tip,action,primary)
                rows[#rows+1]={id=id,label=label,tip=tip,run=action,disabled=not action,primary=primary}
            end
            local progress=tostring(p.before).." → "..p.episode
            row("episode",(p.title and p.title~="" and p.title or "Anime").."   Episode "..p.episode,
                "The AniList entry and episode selected by recognition.")
            row("progress","Progress   "..progress..(p.episode>p.before+1 and " · Episode gap" or ""),
                "Review the absolute progress value before updating AniList.")
            if p.status then
                local status=(statuses[base.status] or "New entry").." → "..statuses[p.status]
                if p.repeat_count then status=status.." · Rewatches "..(base["repeat"] or 0).." → "..p.repeat_count end
                row("status","Status   "..status,"The list status and rewatch count that this update would set.")
            end
            if notice then row("notice",notice,"Result of the last update attempt.") end
            if not busy then
                local action="Update AniList"
                if p.repeat_count then action=p.start_rewatch and "Record completed rewatch" or "Finish rewatch"
                elseif p.start_rewatch then action="Start rewatch and record episode 1"
                elseif p.status=="REPEATING" then action="Resume rewatch and update"
                elseif p.status=="CURRENT" then action=base.id and "Switch to Watching and update" or "Add to Watching and update" end
                row("update",action,"Apply the episode progress and any status change shown above.",function() submit("accept",false) end,true)
                if p.status=="CURRENT" and base.id then
                    row("keep","Update progress only","Keep this status for this entry's later updates.",function() submit("accept",true) end)
                end
                row("skip","Skip this episode","Dismiss this update for the current viewing cycle.",function() submit("skip") end)
            end
            return rows
        end,
    })

    return {
        show=function(value)
            prompt,busy,notice=value,false,nil
            deadline=mp.get_time()+value.seconds
            popup.show("AniList update")
        end,
        close=function(id)
            if not prompt or prompt.id~=id then return end
            prompt=nil
            popup.close()
        end,
        notice=function(message)
            if not prompt then return end
            busy=false
            notice=message
            popup.draw()
        end,
    }
end
