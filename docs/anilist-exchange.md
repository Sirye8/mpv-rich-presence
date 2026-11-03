# AniList sign-in for a public build

The desktop client opens AniList's approval page. The service receives the authorization code, exchanges it for a token, and redirects the browser to mpv on the viewer's computer. The loopback URL carries a one-time ticket. To redeem it, mpv must supply the random verifier it created before opening the browser. Sessions and tickets expire and live only in server memory.

The maintainer runs one copy of `tools/anilist_exchange.py` behind an HTTPS reverse proxy. It listens on loopback. Configure these environment variables through your host's secret or service settings.

| Variable | Value |
| --- | --- |
| `ANILIST_PUBLIC_ORIGIN` | The public HTTPS origin, for example `https://login.example.org`. |
| `ANILIST_CLIENT_ID` | The numeric ID of the registered public integration. |
| `ANILIST_CLIENT_SECRET` | The corresponding AniList client secret, stored only on the server. |
| `PORT` | Internal listener port, default 8787. |

Register `https://login.example.org/callback` as that client's redirect URL. Use the actual hostname in both places. Proxy `/start`, `/callback` and `/redeem` to the internal listener. Disable access logging of callback query strings and request bodies and rate-limit the start endpoint. Run a single worker because pending handoffs live in its memory.

Set `anilist_exchange_url` in the distributed mpv config to the public HTTPS origin. Public mode takes priority over the personal-client option. A server restart cancels unfinished sign-ins. A viewer can start again from the player.

The developer's personal client currently redirects to localhost. Changing its registration to a hosted callback also changes where its personal authorization codes go. Use a second registration if both modes need to work at once.
