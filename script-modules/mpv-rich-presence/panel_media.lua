-- SPDX-License-Identifier: AGPL-3.0-or-later
return function(options, modules, interpreter, utils)
    local shader=utils.join_path(modules,"panel-blur.glsl")
    local parameters={"rp_left","rp_top","rp_right","rp_bottom","rp_radius","rp_strength","rp_method"}
    local methods={classic=0,gaussian=1,box=2}
    local active,geometry=false,nil
    local function stop_blur()
        if not active then return end
        active,geometry=false,nil
        mp.commandv("change-list","glsl-shaders","remove",shader)
        for _,key in ipairs(parameters) do
            mp.commandv("change-list","glsl-shader-opts","remove","panel-blur/"..key)
        end
    end
    options.backdrop=function(bounds,radius,enabled,strength,method)
        if not bounds or not enabled or mp.get_property("current-vo")~="gpu-next" then
            stop_blur()
            return
        end
        local d=mp.get_property_native("osd-dimensions") or {}
        if not d.w or not d.h then stop_blur(); return end
        local l,t,r,b=d.ml or 0,d.mt or 0,d.mr or 0,d.mb or 0
        if bounds[3]<=l or bounds[4]<=t or bounds[1]>=d.w-r or bounds[2]>=d.h-b then
            stop_blur()
            return
        end
        local values={bounds[1]-l,bounds[2]-t,bounds[3]-l,bounds[4]-t,radius,strength or 6,methods[method] or 0}
        local key=table.concat(values,",")
        if geometry==key then return end
        local args=mp.get_property_native("glsl-shader-opts") or {}
        for i,name in ipairs(parameters) do
            args["panel-blur/"..name]=string.format("%.2f",values[i])
        end
        if not mp.set_property_native("glsl-shader-opts",args) then return end
        if not active then
            active=not not mp.commandv("change-list","glsl-shaders","append",shader)
            if not active then
                for _,name in ipairs(parameters) do
                    mp.commandv("change-list","glsl-shader-opts","remove","panel-blur/"..name)
                end
            end
        end
        if active then geometry=key end
    end

    -- One overlay slot per image. Each decodes its URL once through avatar.py and is redrawn when it moves.
    local slots={}
    local function image_slot(id,mode,decode,allowed)
        local s={file=utils.join_path(os.getenv("TEMP") or os.getenv("TMPDIR") or "/tmp",
            "mpv-rich-presence-"..mode.."-"..mp.get_property("pid")..".bgra")}
        slots[#slots+1]=s
        local function remove()
            if s.displayed then mp.commandv("overlay-remove",id); s.displayed=nil end
        end
        s.remove=remove
        local function show()
            if not s.rect or not s.pixels then return end
            local key=table.concat(s.rect,":")
            if s.displayed==key then return end
            local x,y,size=s.rect[1],s.rect[2],s.rect[3]
            if mp.commandv("overlay-add",id,x,y,s.file,0,"bgra",decode,decode,decode*4,size,size) then
                s.displayed=key
            end
        end
        local function load(url,x,y,size)
            if type(url)~="string" or not allowed(url) or not x then
                s.rect=nil
                remove()
                return
            end
            s.rect={x,y,size}
            if url==s.url and (s.pixels or s.job or mp.get_time()<(s.retry or 0)) then
                show()
                return
            end
            if s.job then mp.abort_async_command(s.job) end
            remove()
            s.url,s.pixels=url,nil
            s.job=mp.command_native_async({name="subprocess",args={interpreter,"-I",
                utils.join_path(modules,"avatar.py"),url,mp.get_property("pid"),mode},
                playback_only=false,capture_stdout=true,capture_stderr=true},function(_,result)
                if s.url~=url then return end
                s.job=nil
                s.retry=mp.get_time()+60
                if not result or result.status~=0 or #(result.stdout or "")~=decode*decode*4 then
                    mp.msg.verbose("Could not load a panel image. The panel can retry in 60 seconds.")
                    return
                end
                local file=io.open(s.file,"wb")
                if not file then return end
                local written=file:write(result.stdout)
                file:close()
                if written then s.pixels=true; show() end
            end)
        end
        return load,function(url) return url==s.url and s.pixels==true end
    end

    options.avatar=image_slot(58,"avatar",64,function(url)
        return url:match("^https://s4%.anilist%.co/") or url:match("^https://lastfm%-img%.freetls%.fastly%.net/")
    end)
    local public=function(url) return url:match("^https://[%w%.%-]+/") ~= nil end
    local large,large_ready=image_slot(59,"cover",128,public)
    local small,small_ready=image_slot(60,"badge",64,public)
    options.card_images={large_ready=large_ready,small_ready=small_ready,
        show=function(large_url,x,y,size,small_url,bx,by,bsize)
            large(large_url,x,y,size)
            small(small_url,bx,by,bsize)
        end}
    mp.register_event("shutdown",function()
        for _,s in ipairs(slots) do
            if s.job then mp.abort_async_command(s.job) end
            s.remove()
            os.remove(s.file)
        end
        stop_blur()
    end)
end
