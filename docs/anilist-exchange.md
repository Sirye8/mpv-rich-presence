# AniList sign-in on Cloudflare Workers Free

The Worker handles browser approval for client 55822. Viewers connect in mpv and approve their own account on AniList. Their plugin then reads and updates their list directly. Playback and Discord updates do not contact the Worker.

## Deployment

Use Workers Free with the supplied `tools/cloudflare/wrangler.jsonc`. The Worker needs no database, custom domain, payment method or background keepalive. Its public URL uses the account's workers.dev subdomain.

1. Deploy `tools/cloudflare/worker.mjs` to a Worker named `mpv-anilist-login`. When using Wrangler, run `wrangler deploy --config tools/cloudflare/wrangler.jsonc` from the repository.
2. In the Worker's settings, add a **Secret** named `ANILIST_CLIENT_SECRET`. Enter the registered client's secret in the dashboard. Keep invocation logging and tracing disabled for this endpoint.
3. Open `/health` on the Worker. Copy the exact `redirect_uri` into the AniList application registration. The health response contains the public client ID and a configuration flag.
4. Set the plugin's public sign-in origin to the Worker's HTTPS origin. Test Connect with a fresh mpv config.

## Handoff

On Connect, mpv checks the service, generates a random state and verifier, and sends only the state and verifier challenge to start approval. The callback sends a short-lived encrypted ticket to the loopback listener. The Worker redeems the enclosed single-use AniList authorization code only for the holder of the verifier. It returns the token over HTTPS and keeps no account or token database. The desktop worker checks Viewer before saving credentials.

The Free plan has a daily request ceiling. Hitting it interrupts new sign-ins until the quota resets. Existing tokens continue working. See [Workers limits](https://developers.cloudflare.com/workers/platform/limits/#daily-requests).
