# Public AniList sign-in

Viewers select Connect in mpv and approve their own account on AniList. The plugin includes client `55822` and the public service address. Only the service owner needs hosting accounts or an application secret.

## Request path

```text
mpv / browser -> Cloudflare relay -> Netlify exchange -> AniList OAuth
mpv          ---------------------------------------> AniList GraphQL
```

The relay at `https://mpv-anilist-login.si-based.workers.dev` forwards four fixed sign-in routes to `https://mpv-anilist-login.netlify.app`. Netlify stores the AniList application secret and exchanges authorization codes. Authenticated AniList list requests go directly to AniList. The Worker also has a separate route for [shared TMDB metadata](tmdb-metadata.md).

This split addresses two failures observed during deployment. AniList rejected token requests from the Worker, and the test PC could not connect directly to Netlify. Cloudflare could reach Netlify, and Netlify could reach AniList. A read-only test on 6 November 2026 verified the full route, including the Viewer query and session-only sign-in, on a fresh Windows mpv installation.

## Owner setup

1. Keep the Netlify project on Free. Deploy `tools/login-site` and the function in `tools/netlify`, using the root `netlify.toml`. Make the production project public.
2. In Netlify's environment settings, add `ANILIST_CLIENT_SECRET`, mark it as a secret, and set its value for **Production only**. Do not set a local-development value. Deploy again after changing the secret.
3. Deploy `tools/cloudflare/worker.mjs` with `wrangler deploy --config tools/cloudflare/wrangler.jsonc`. The relay does not use a client secret. Keep invocation logging and tracing disabled for the sign-in routes.
4. Check the Worker's `/health` response. It must report `configured: true` and the intended client ID. Register its exact `redirect_uri` in the AniList application's settings. For this deployment that is `https://mpv-anilist-login.si-based.workers.dev/callback`.
5. Run `node --test tests/cloudflare.test.mjs`. To check a real account without editing its list, run `python -B tests/live_signin.py <installed-package> <mpv-executable>` and approve in the browser. The test uses a disposable mpv config and closes the session after verifying Viewer.

For another deployment, update the fixed upstream in `tools/cloudflare/worker.mjs`, the public origin in `tools/netlify/anilist.mjs`, and the Netlify `ANILIST_CLIENT_ID` environment variable. Set the plugin's `anilist_exchange_url` to the new public origin and set `anilist_client_id` to match. Test the new registration before distributing it.

## Handoff and credentials

On Connect, mpv checks the service, generates a random state and verifier, and sends only the state and verifier challenge to start approval. The authorization state expires after three minutes. The callback returns a 90-second encrypted ticket to the fixed localhost listener. Netlify redeems the enclosed single-use AniList code only for the holder of the original verifier. It returns the access token over HTTPS and keeps no account or token database. The desktop helper checks Viewer and displays the profile on localhost. It saves the connection after the user confirms it.

Neither service logs request bodies or tokens in application code. Do not enable request tracing or add logging of callback URLs, tickets, authorization headers or token responses. Cloudflare necessarily forwards the encrypted ticket and the HTTPS token response. Both hosting providers are part of the trust boundary during sign-in.

Windows uses Credential Manager. Linux can use keyring with Secret Service. Session-only login keeps the token in memory. Existing tokens continue to work if either sign-in host is unavailable; reconnecting requires the hosts to recover.

## Free-tier limits

No browser window, scheduled ping, Docker container or continuously running server is needed for this service. Connect checks availability before opening the authorization page and reports failures in mpv.

Netlify's credit-based Free plan has a hard monthly allowance. Production deploys, requests, bandwidth and function execution count toward it. Cloudflare Workers Free has a daily request ceiling shared with TMDB lookups. Exhausting either allowance can interrupt new sign-ins. Keep the deployment on the free plans and check the providers' current terms. [Netlify credit-based plans](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/credit-based-pricing-plans/) · [Cloudflare Workers limits](https://developers.cloudflare.com/workers/platform/limits/#daily-requests)

Both services and AniList must remain available. Changes to provider rules, quotas or network access can require maintenance. Check `/health`, then a real Viewer sign-in after changing any host, callback or credential. The health check alone does not prove that AniList will accept a code exchange.
