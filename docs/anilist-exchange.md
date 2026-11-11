# Public AniList sign-in

Viewers select Connect in mpv and approve their own account on AniList. The plugin includes client `55822` and the public service address. Only the service owner needs hosting accounts or an application secret.

## Request path

```text
mpv / browser -> Cloudflare relay -> Netlify exchange -> AniList OAuth
mpv          ---------------------------------------> AniList GraphQL
```

The relay at `https://mpv-anilist-login.si-based.workers.dev` forwards four fixed sign-in routes to `https://mpv-anilist-login.netlify.app`. Netlify stores the AniList application secret and exchanges authorization codes. Authenticated AniList list requests go directly to AniList. The Worker also has a separate route for [shared TMDB metadata](tmdb-metadata.md).

This split addresses two failures observed during deployment: AniList rejected token requests from the Worker, while the test PC could not connect directly to the Netlify domain. Cloudflare could reach Netlify, and Netlify could reach AniList. The complete route passed a read-only sign-in test from a fresh Windows mpv installation on 6 November 2026, including the authenticated Viewer query and session-only storage. This verifies that route at the time of the test, not availability on every network.

## Owner setup

1. Keep the Netlify project on Free. Deploy `tools/login-site` and the function in `tools/netlify`, using the root `netlify.toml`. Make the production project public.
2. In Netlify's environment settings, add `ANILIST_CLIENT_SECRET`, mark it as a secret, and set its value for **Production only**. Do not set a local-development value. Deploy again after changing the secret.
3. Deploy `tools/cloudflare/worker.mjs` with `wrangler deploy --config tools/cloudflare/wrangler.jsonc`. The relay does not use a client secret. Keep invocation logging and tracing disabled for the sign-in routes.
4. Check the Worker's `/health` response. It must report `configured: true` and the intended client ID. Register its exact `redirect_uri` in the AniList application's settings. For this deployment that is `https://mpv-anilist-login.si-based.workers.dev/callback`.
5. Run `node --test tests/cloudflare.test.mjs`. To check a real account without editing its list, run `python -B tests/live_signin.py <installed-package> <mpv-executable>` and approve in the browser. The test uses a disposable mpv config and closes the session after verifying Viewer.

For another deployment, update the fixed upstream in `tools/cloudflare/worker.mjs`, the public origin in `tools/netlify/anilist.mjs`, and the client ID. Set the plugin's `anilist_exchange_url` to that public origin. Test the new registration before distributing it.

## Handoff and credentials

On Connect, mpv checks the service, generates a random state and verifier, and sends only the state and verifier challenge to start approval. The authorization state expires after three minutes. The callback returns a 90-second encrypted ticket to the fixed localhost listener. Netlify redeems the enclosed single-use AniList code only for the holder of the original verifier. It returns the access token over HTTPS and keeps no account or token database. The desktop helper checks Viewer before storing credentials.

Neither service logs request bodies or tokens in application code. Do not enable request tracing or add logging of callback URLs, tickets, authorization headers or token responses. Cloudflare necessarily forwards the encrypted ticket and the HTTPS token response. Both hosting providers are part of the trust boundary during sign-in.

Windows uses Credential Manager. Linux can use keyring with Secret Service. Session-only login keeps the token in memory. Existing tokens continue to work if either sign-in host is unavailable; reconnecting requires the hosts to recover.

## Free-tier limits

No browser window, scheduled ping, Docker container or continuously running server is needed for this service. Connect checks availability before opening the authorization page and reports failures in mpv.

Netlify Free currently includes 300 credits per month with a hard limit. Production deploys use 15 credits each, and requests, bandwidth and function execution also use the allowance. Keep deployments deliberate and monitor usage. Cloudflare Workers Free has a daily request ceiling. Exhausting either allowance can interrupt new sign-ins. See [Netlify pricing](https://www.netlify.com/pricing/) and [Workers limits](https://developers.cloudflare.com/workers/platform/limits/#daily-requests) for current terms. Do not enable a paid plan or automatic top-ups for this deployment.

Both services and AniList must remain available. Changes to provider rules, quotas or network access can require maintenance. Check `/health`, then a real Viewer sign-in after changing any host, callback or credential. The health check alone does not prove that AniList will accept a code exchange.
