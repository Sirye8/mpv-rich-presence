-- SPDX-License-Identifier: AGPL-3.0-or-later
local help = {
    ["Now playing"]="See the current match, outgoing Discord card, and sharing status.",
    ["AniList"]="Connect an account, check list progress, and manage watch tracking.",
    ["Appearance"]="Preview the Discord card and save changes for a file, folder, or all media.",
    ["Recognition & folders"]="Choose a recognition source or correct a match for a file or folder.",
    ["Privacy & windows"]="Control sharing, lookups, tracking, and the active Discord window.",
    ["Saved data"]="Review saved settings, matches, cache entries, and local watch history.",
    ["Diagnostics"]="See the current provider, privacy rules, and recent Discord or AniList results.",
    ["View outgoing card"]="Open the read-only preview of the text, artwork and links Discord receives.",
    ["Preview"]="Open the read-only card preview with your current Appearance draft.",
    ["Correct match"]="Open recognition settings to change the source, title match or episode offset.",
    ["Hide this file"]="Hide Discord for this file. The separate tracking permission still applies.",
    ["Share this file"]="Remove the temporary hide for this file.",
    ["Use this window for Discord"]="Make this window the active Discord publisher.",
    ["View AniList progress"]="Open the list entry for the current anime.",
    ["AniList progress"]="Open the list entry for the current anime.",
    ["Save for"]="Choose where this change is saved.",
    ["Media layout"]="Set the card for all media or just one media type.",
    ["Preset"]="Original preserves the old layout. Clean removes extra details. Detailed adds context. Private uses a generic card.",
    ["Title language"]="Choose the anime title language. Missing translations fall back to the original title.",
    ["Episode numbering"]="Show the season-relative number, the original filename number, or both.",
    ["Timer"]="Choose how Discord shows time. AniList watch time uses a separate counter.",
    ["Release group"]="Show or hide the release group on the anime episode line.",
    ["Episode"]="Show or hide the anime episode number.",
    ["Artwork"]="Show available cover art or a poster from the matched source.",
    ["Badge"]="Show the small playback status icon.",
    ["Tooltip"]="Show playback status when someone hovers over the Discord badge.",
    ["Provider button"]="Show or hide the default link to the matched entry on its metadata provider.",
    ["Details"]="Show or hide the first text line under the activity name.",
    ["State"]="Show or hide the second text line under the activity name.",
    ["Compact status"]="Choose the card field Discord uses for compact status.",
    ["Episode titles"]="Show matched TV episode titles. Keep it off to avoid spoilers.",
    ["Headline"]="Set the activity name. Tokens include {title}, {artist}, {album}, {episode}, {original}, {group}, {channel}, {details}, {state}, and {year}.",
    ["Details template"]="Edit the first line using tokens such as {title} and {artist}. In {artist}|{title}, each piece is kept only if its tokens have values. Preview the result and choose Apply to save.",
    ["State template"]="Edit the second line using tokens such as {episode}, {album} and {year}. Separate optional pieces with |. A piece with a missing token is omitted. Preview the result and choose Apply to save.",
    ["Artwork link"]="Enter a public HTTP or HTTPS URL for the artwork, or clear the field to remove it.",
    ["Details link"]="Enter a public HTTP or HTTPS URL for the first text line, or clear the field to remove it.",
    ["State link"]="Enter a public HTTP or HTTPS URL for the second text line, or clear the field to remove it.",
    ["Edit public buttons"]="Enter Label|https://example.com. For two buttons, separate the entries with a comma. These replace the default provider button. Submit an empty value to hide all buttons.",
    ["Reset saved appearance"]="Clear the overrides at the chosen scope so it inherits its parent settings.",
    ["Apply"]="Save the preview to the selected scope.",
    ["Cancel"]="Discard the current action or unsaved preview.",
    ["Confirm"]="Apply the action described above to the selected scope.",
    ["Media recognition"]="Choose which providers can look up media for this scope. A file rule takes priority over the nearest folder rule, then the global panel rule, then the conf default. Choose Inherit to remove the current override.",
    ["Inherit recognition source"]="Remove this override and use the next applicable folder, global, or conf setting.",
    ["Automatic"]="Run the AniList anime matcher first for video. If it completes without a match, try movie or TV providers. For local audio, start with MusicBrainz tags and try AcoustID if the tags do not identify a match.",
    ["Anime"]="Use AniList's anime matcher only. If nothing matches, use the local title.",
    ["Movies & TV"]="Skip AniList. Episode markers select TV. Other titles try movies, then TV if no movie matches.",
    ["Movies"]="Skip AniList and TV. Use TMDB movies, with Wikidata as a fallback.",
    ["TV"]="Skip AniList and movies. Use TMDB TV, with TVmaze as a fallback. Dated episodes use TVmaze.",
    ["Original"]="Use AniList for anime and mpv's local title or tags for other media.",
    ["TMDB movies"]="Match movies and posters with the shared TMDB service or your own token.",
    ["TMDB TV"]="Match TV series and episodes with the shared TMDB service or your own token.",
    ["TVmaze"]="Match TV shows and find episodes by season and number or air date.",
    ["Wikidata"]="Match film titles and release years using public Wikidata records.",
    ["MusicBrainz"]="Match music by its tags and fetch artwork for an identified release.",
    ["Stream metadata"]="Use the media title and channel reported by mpv.",
    ["AcoustID"]="Match local audio by fingerprint. The package includes fpcalc and the application key.",
    ["Provider settings and credits"]="See provider requirements and which optional keys are configured.",
    ["AniList search"]="Search anime titles by name and choose a match.",
    ["Enter AniList ID or URL"]="Enter a positive AniList ID or an anilist.co/anime URL.",
    ["Episode offset"]="Adjust the episode number used for matching and AniList progress. For example, enter -12 when the filename says episode 13 and this season starts at episode 1. Enter 0 for the normal mapping.",
    ["Mark as non-anime"]="Skip AniList matching for this file or folder.",
    ["Refresh match"]="Run recognition again while keeping saved corrections.",
    ["Discord"]="Choose what this scope shares.",
    ["Start private session"]="Stop Discord sharing, lookups, and tracking for this session.",
    ["End private session"]="Restore the usual file and folder privacy rules.",
    ["Exclude this folder"]="Review blocking Discord, recognition, and tracking in this folder and its subfolders.",
    ["Hide Discord for a time"]="Enter a whole number from 1 to 1440 minutes. Enter 0 to end the timed hide.",
    ["Hide Discord until this window closes"]="Hide this window's Discord presence until it closes.",
    ["End window hide"]="Restore the usual sharing rules for this window.",
    ["Pin this window for Discord"]="Keep this eligible window as the Discord publisher.",
    ["Return to last-resumed window"]="Release the pin so the last resumed eligible window can publish.",
    ["Use this window"]="Make this window the active Discord publisher.",
    ["Track while hidden"]="Allow tracking while Discord is hidden. Private sessions and exclusions still block it.",
    ["Connect account"]="Approve sign-in in your browser and use the OS credential store when available.",
    ["Connect for this session"]="Approve sign-in in your browser and keep the credential in memory until mpv closes.",
    ["Reconnect to AniList"]="Renew the expired AniList session through the browser.",
    ["Switch or reconnect account"]="Open browser sign-in again. Switch accounts in your browser first if needed.",
    ["Refresh list entry"]="Read the current episode count and status from AniList.",
    ["Disconnect"]="Choose whether to keep or forget this account's local watch history.",
    ["Keep local watch history"]="Disconnect and cancel pending updates. Keep local counters and history.",
    ["Forget local watch history"]="Disconnect and clear this account's local tracking records and cached entries.",
    ["−  One episode"]="Decrease the draft by one, down to zero.",
    ["+  One episode"]="Increase the draft by one, up to the known episode total.",
    ["Draft"]="Proposed episode count. Use Apply progress to review it.",
    ["Status on Apply"]="Keep the current list status or choose a different status for this edit.",
    ["Apply progress"]="Review the progress and status change before sending it to AniList.",
    ["Reset draft"]="Restore the saved episode count in the draft.",
    ["Mark current episode watched"]="Review setting your progress to the episode playing now.",
    ["Tracking"]="Choose when to record watch time and how list updates are applied.",
    ["Resume unfinished time"]="Keep the watch counter when you come back to an unfinished episode.",
    ["Keep offline proposals"]="Keep pending proposals locally while the connection is unavailable.",
    ["Pending updates"]="Review episodes that reached the watch threshold. Before applying, open the matching episode so the plugin can verify the episode and refresh AniList progress. Skipping suppresses automatic updates in this viewing cycle.",
    ["Rewatch tracking"]="Enable separate watch counters for episodes you watch again.",
    ["Start new viewing cycle"]="Start a separate local watch counter for this episode.",
    ["Threshold"]="Edit the minimum watch time per episode for Ask and Auto. Enter a whole number from 1 to 1440 minutes.",
    ["Later"]="Keep the proposal pending and return to the previous page.",
    ["Skip this episode"]="Dismiss the proposal and suppress automatic credit for the episode.",
    ["Filter records"]="Enter part of a record category or scope to narrow the list.",
    ["Reset everything"]="Review a local reset of settings, cache, watch history, and account connection.",
    ["Fingerprint this file"]="Analyze up to the first two minutes of the local audio and look up the resulting fingerprint on AcoustID. Review the results before saving a match. The package supplies fpcalc and the application key.",
    ["Sharing"]="Why this window is sharing or hidden.",
    ["SDK"]="Latest result from the Discord SDK.",
    ["Configuration"]="Whether the plugin received its configuration from Lua.",
    ["Publisher"]="Which mpv window currently owns the Discord card.",
    ["Lookup"]="Whether the active privacy rules permit recognition requests.",
    ["Recognition source"]="Provider supplying the metadata for this file.",
    ["Source rule"]="Scope of the active recognition setting.",
    ["Match rule"]="Scope of the active match correction.",
    ["Appearance rule"]="Scope of the active card settings.",
    ["Last list read"]="Time of the latest successful AniList read.",
    ["Last list write"]="Time of the latest confirmed AniList update.",
    ["AniList ID"]="Matched anime entry ID. Zero means there is no AniList match.",
    ["Account operation"]="Latest message from the account or recognition helper.",
}

local choices = {
    ["Save for"]={global="Apply to all media.",folder="Apply to this folder and its subfolders.",file="Apply only to the current file."},
    ["Media layout"]={all="Every media type.",anime="Matched anime.",audio="Unmatched audio.",video="Unmatched video.",
        movie="Matched movies.",tv="Matched TV episodes or shows.",recording="Matched music.",stream="Matched streams."},
    ["Preset"]={original="Use the v1.0.10 layout.",clean="Keep the card compact and omit the release group.",
        detailed="Include more context where available.",private="Use a generic card."},
    ["Title language"]={original="Use the original match title.",english="Prefer the English title.",
        romaji="Prefer the Romanized title.",native="Prefer the native-script title."},
    ["Episode numbering"]={relative="Use the matched season’s numbering.",original="Use the number from the filename.",
        both="Show both when they differ."},
    ["Timer"]={progress="Show playback progress.",remaining="Show the time left.",session="Show time since the file opened.",
        off="Hide the Discord timer."},
    ["Compact status"]={name="Use the activity name.",details="Use the first text line.",state="Use the second text line."},
    ["Discord"]={full="Share the full card.",generic="Share a plain card without the media details.",
        hidden="Hide this activity on Discord."},
    ["Status on Apply"]={unchanged="Keep the saved status.",CURRENT="Mark as watching.",COMPLETED="Mark as completed.",
        REPEATING="Mark as rewatching.",PAUSED="Mark as paused.",DROPPED="Mark as dropped.",PLANNING="Mark as planned."},
    ["Tracking"]={off="Stop recording watch time.",manual="Record time. Apply list changes yourself.",
        ask="Review updates at the threshold.",auto="Apply eligible next episodes at the threshold."},
    ["Panel position"]={config="Use the position in rich-presence.conf.",
        ["top-left"]="Upper left, the default.",["top-center"]="Centered at the top.",["top-right"]="Upper right.",
        ["bottom-left"]="Lower left above playback controls.",["bottom-center"]="Centered above playback controls.",
        ["bottom-right"]="Lower right above playback controls."},
    ["Track while hidden"]={On="Allow tracking without Discord sharing.",Off="Require normal sharing permission."},
    ["Resume unfinished time"]={On="Resume the saved counter.",Off="Start over when opening the episode."},
    ["Keep offline proposals"]={On="Keep pending proposals locally.",Off="Wait for AniList before creating a proposal."},
    ["Rewatch tracking"]={On="Allow starting a new viewing cycle.",Off="Use the existing viewing cycle."},
    ["Release group"]={On="Show the release group on the episode line.",Off="Hide the release group."},
    Episode={On="Show the anime episode number.",Off="Hide the episode number."},
    Artwork={On="Show available cover art.",Off="Hide the large artwork."},
    Badge={On="Show the playback status icon.",Off="Hide the status icon."},
    Tooltip={On="Show status when hovering over the badge.",Off="Hide the badge’s hover text."},
    ["Provider button"]={On="Show the default provider link.",Off="Hide the default provider link."},
    Details={On="Show the first text line.",Off="Hide the first text line."},
    State={On="Show the second text line.",Off="Hide the second text line."},
    ["Episode titles"]={On="Show matched TV episode titles.",Off="Hide episode titles to avoid spoilers."},
}

local read_only = {
    ["Card preview"]={
        Activity="Activity type and name that Discord receives.",
        Details="First text line under the activity name. Change it in Appearance.",
        State="Second text line under the activity name. Change it in Appearance.",
        Artwork="Image URL for the large artwork on the card.",
        ["Artwork tooltip"]="Text Discord shows when someone hovers over the large artwork.",
        Badge="Image URL for the small playback status badge.",
        ["Badge tooltip"]="Text Discord shows when someone hovers over the badge.",
        ["Compact status"]="Field selected for Discord’s compact status.",
        ["Artwork link"]="Public link attached to the large artwork.",
        ["Details link"]="Public link attached to the first text line.",
        ["State link"]="Public link attached to the second text line.",
        ["Public button"]="This label and URL are sent to Discord. The button can be changed in Appearance.",
    },
    ["Now playing"]={Sharing="Current Discord sharing status.",Activity="Activity type and name on the current card.",
        Details="First text line on the current Discord card.",State="Second text line on the current Discord card."},
    ["AniList"]={Account="Account connected in mpv.",Anime="Title of the matched anime.",
        ["Playing episode"]="Season-relative episode number used for AniList.",
        ["Episode watch time"]="Actual time spent in eligible playback in this viewing cycle. Pauses, buffering and seeks add no time. At 2×, a real minute still adds one minute.",
        ["Watch started"]="Local clock time of the first credited playback interval in this viewing cycle. Older records may only have elapsed time.",
        ["Last watched"]="Local clock time of the latest credited playback interval in this viewing cycle.",
        Draft="Proposed episode count. Use the plus and minus controls to change it.",
        ["List entry"]="Most recently confirmed AniList progress and status for this anime.",
        ["List status"]="Whether mpv has a current AniList entry for this anime."},
    ["Recognition & folders"]={["Source rule"]="Scope that supplied the active recognition choice.",
        ["Open a local file to set its folder source"]="A local file must be open before you can assign a folder rule."},
    Providers={AniList="Public anime metadata and artwork. Signing in is only needed for list features.",
        TMDB="Movie and TV metadata uses the shared service or the optional tmdb_key in rich-presence.conf.",
        TVmaze="TV metadata and aired episode order. Data is licensed under CC BY-SA.",
        Wikidata="Public film titles and release dates.",MusicBrainz="Public music recording and release data.",
        ["Cover Art Archive"]="Artwork for matched MusicBrainz releases.",
        ["TMDB attribution"]="TMDB supplies the metadata and posters used by this project."},
    ["Saved match"]={Scope="File, folder or global scope of this saved correction."},
    ["Disconnect account"]={Account="The account that the selected action will disconnect from mpv."},
    ["Pending updates"]={Proposal="Anime and episode covered by this local proposal.",
        ["Waiting to refresh remote progress"]="Refresh the AniList entry before applying this proposal.",
        ["Play this episode to apply its update"]="Open the matching episode so mpv can verify the proposal’s playback context.",
        ["No pending updates"]="There are no saved proposals for the connected account."},
    ["Search results"]={["Searching…"]="Waiting for the selected provider to return matches.",
        ["No results"]="The provider returned no matches for this search."},
}

return function(label, page, action, values, current, key)
    key=key or label:match("^(.-)   ") or label
    if values then
        local descriptions=choices[key]
        local options={}
        for _,value in ipairs(values) do
            options[#options+1]={label=value,selected=value==current,
                detail=descriptions and descriptions[value] or
                    (value=="On" and (help[key] or "Enable this setting.") or "Leave this off.")}
        end
        return {options=options}
    end
    if not page then return help[key] end
    if not action then
        local fields=read_only[page] or {}
        local description=fields[key]
        if key=="Message" then description="Latest status message from mpv or the account helper."
        elseif page=="Card preview" and key:match("^Public button %d+$") then description=fields["Public button"]
        elseif page=="Pending updates" and key:match("^Proposal ") then description=fields.Proposal
        elseif page=="Diagnostics" then description=help[key]
        elseif page=="Confirm" then description="Review the proposed action before you confirm."
        elseif page=="Providers" then description=description or "Provider attribution or setup information." end
        local value=label:match("^.-   (.+)$")
        if page=="AniList" and (key=="Watch started" or key=="Last watched") and value then
            description=value..". "..description
        end
        return "Read only. "..(description or "Information for this page.")
    end
    if page=="Saved match" then return "Review removing this field from the saved correction. Other fields stay saved." end
    if page=="Search results" then return "Review saving this provider match at the selected scope." end
    if key:match("^Clear ") then return "Review which local records in this category will be removed." end
    if page=="Saved data" and not help[key] then return "Open this local record or review removing it at the displayed scope." end
    if key:match("^Search ") then return "Enter a title to find matches from this provider." end
    if key:match("^Update episode ") or key:match("^Apply ") then return "Review the proposed change before sending it to AniList." end
    if key:match("^Skip ") then return "Dismiss the proposal and suppress automatic updates for this episode." end
    return help[key] or "Select to continue."
end
