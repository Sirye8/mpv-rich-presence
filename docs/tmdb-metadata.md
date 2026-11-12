# Shared TMDB metadata

Automatic recognition prefers TMDB once it reaches movie or TV lookup. Movie matches can show the release year and poster. TV matches can show the series and episode number. Wikidata and TVmaze provide fallback lookups. TVmaze handles air-date filenames.

Leave `tmdb_key` empty to use the shared service. For direct lookups, enter your own TMDB API Read Access Token in the local config. See the [settings reference](user-guide.md#account-helper-and-provider-access).

The existing `mpv-anilist-login` Cloudflare Worker serves a separate `/metadata/` route for movie and TV search and details. It sends only the metadata fields used by the plugin. Viewer requests contain a cleaned title or public provider ID. The Worker rejects paths, arbitrary destinations, and unrelated TMDB routes. The desktop caches results locally. TMDB requests have their own rate limiter and use the account's existing Workers Free request allowance.

## Owner setup

1. Register the project with TMDB and describe the public, non-commercial desktop plugin and its shared server lookup. Use the application's API Read Access Token. See [TMDB application authentication](https://developer.themoviedb.org/docs/authentication-application).
2. Deploy the Worker from `tools/cloudflare/wrangler.jsonc`. In its production **Variables and secrets**, save the token as a **Secret** named `TMDB_READ_TOKEN`. Keep the token out of source, packages, and log output.
3. Check `/metadata/health` for `configured: true`, then verify a movie search and a TV episode lookup. The AniList `/health` and callback use the existing Netlify exchange.

## Credits

[<img src="https://www.themoviedb.org/assets/v4/logos/v2/blue_short-8e7b30f73a4020692ccca9c88bafe5dcb6f8a62a4c6bc55cd9ba82bb2cd95f6c.svg" alt="TMDB" width="96">](https://www.themoviedb.org/)

This product uses the TMDB API but is not endorsed or certified by TMDB.
