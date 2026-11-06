// SPDX-License-Identifier: AGPL-3.0-or-later
// The ticket carries AniList's single-use code. The viewer's token is never stored.
const utf8 = new TextEncoder();
const loopback = "http://127.0.0.1:47653/callback";
const headers = { "Cache-Control": "no-store", "Referrer-Policy": "no-referrer" };

function json(status, body) {
  return Response.json(body, { status, headers });
}

function base64url(bytes) {
  return btoa(String.fromCharCode(...bytes)).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

function decode(text) {
  return Uint8Array.from(atob(text.replaceAll("-", "+").replaceAll("_", "/")), ch => ch.charCodeAt(0));
}

async function sessionKey(env, origin) {
  const key = await crypto.subtle.importKey("raw", utf8.encode(env.ANILIST_CLIENT_SECRET), "HKDF", false, ["deriveKey"]);
  return crypto.subtle.deriveKey({
    name: "HKDF", hash: "SHA-256", salt: utf8.encode(origin),
    info: utf8.encode("mpv-rich-presence/oauth/v1/" + env.ANILIST_CLIENT_ID)
  }, key, { name: "AES-GCM", length: 256 }, false, ["encrypt", "decrypt"]);
}

async function seal(key, purpose, value, seconds) {
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const data = utf8.encode(JSON.stringify({ ...value, expires: Date.now() + seconds * 1000 }));
  const encrypted = await crypto.subtle.encrypt({ name: "AES-GCM", iv, additionalData: utf8.encode(purpose) }, key, data);
  return base64url(iv) + "." + base64url(new Uint8Array(encrypted));
}

async function unseal(key, purpose, token) {
  if (typeof token !== "string" || token.length > 6400 || !/^[\w-]{16}\.[\w-]+$/.test(token)) throw Error();
  const [iv, data] = token.split(".").map(decode);
  const bytes = await crypto.subtle.decrypt({ name: "AES-GCM", iv, additionalData: utf8.encode(purpose) }, key, data);
  const value = JSON.parse(new TextDecoder().decode(bytes));
  if (value.expires <= Date.now()) throw Error();
  return value;
}

async function payload(request) {
  const length = Number(request.headers.get("Content-Length"));
  if (!Number.isInteger(length) || length < 1 || length > 8192) throw Error();
  const raw = await request.text();
  if (raw.length > 8192) throw Error();
  return JSON.parse(raw);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const origin = env.ANILIST_PUBLIC_ORIGIN || url.origin;
    const redirect = origin + "/callback";
    const client = env.ANILIST_CLIENT_ID || "55822";
    if (request.method === "GET" && url.pathname === "/health") {
      return json(200, { service: "mpv-anilist-login", client_id: client, redirect_uri: redirect,
        configured: Boolean(env.ANILIST_CLIENT_SECRET) });
    }
    if (!env.ANILIST_CLIENT_SECRET) return json(503, { error: "Owner setup required." });
    let step = "request validation";
    try {
      const key = await sessionKey(env, origin);
      if (request.method === "POST" && url.pathname === "/start") {
        const { state, challenge } = await payload(request);
        if (typeof state !== "string" || !/^[\w-]{32,96}$/.test(state) ||
            typeof challenge !== "string" || !/^[\w-]{43}$/.test(challenge)) throw Error();
        const authorize = new URL("https://anilist.co/api/v2/oauth/authorize");
        authorize.search = new URLSearchParams({ client_id: client, response_type: "code", redirect_uri: redirect,
          state: await seal(key, "approval", { state, challenge }, 180), code_challenge: challenge,
          code_challenge_method: "S256" }).toString();
        return json(200, { authorize: authorize.href });
      }
      if (request.method === "GET" && url.pathname === "/callback") {
        const { state, challenge } = await unseal(key, "approval", url.searchParams.get("state"));
        const code = url.searchParams.get("code");
        if (!code || code.length > 4096) throw Error();
        const dest = new URL(loopback);
        dest.search = new URLSearchParams({ state, code: await seal(key, "ticket", { code, challenge }, 90) }).toString();
        return new Response(null, { status: 303, headers: { ...headers, Location: dest.href } });
      }
      if (request.method === "POST" && url.pathname === "/redeem") {
        const { ticket, verifier } = await payload(request);
        if (typeof verifier !== "string" || !/^[A-Za-z0-9._~-]{43,128}$/.test(verifier)) throw Error();
        step = "ticket validation";
        const { code, challenge } = await unseal(key, "ticket", ticket);
        step = "proof validation";
        const digest = await crypto.subtle.digest("SHA-256", utf8.encode(verifier));
        if (base64url(new Uint8Array(digest)) !== challenge) throw Error();
        step = "AniList token request";
        const result = await fetch("https://anilist.co/api/v2/oauth/token", {
          method: "POST", redirect: "manual", signal: AbortSignal.timeout(10000),
          headers: { Accept: "application/json", "Content-Type": "application/json",
            "User-Agent": "mpv-rich-presence/1.0 (https://github.com/Sirye8/mpv-rich-presence)" },
          body: JSON.stringify({ grant_type: "authorization_code", code, code_verifier: verifier,
            client_id: client, client_secret: env.ANILIST_CLIENT_SECRET, redirect_uri: redirect })
        });
        if (!result.ok) {
          // Return only a status and a known OAuth error name, never the upstream body.
          const responseText = await result.text();
          let failure = {};
          try { failure = JSON.parse(responseText); } catch {}
          const known = ["invalid_client", "invalid_grant", "invalid_request", "unsupported_grant_type", "access_denied"];
          const edgeCode = responseText.match(/error(?:\s|<[^>]*>)*(?:code|[:#])?\s*(10\d\d)/i)?.[1];
          const errorName = typeof failure.error === "string" && /^[a-zA-Z_ ]{1,48}$/.test(failure.error) ? failure.error : "";
          const blocked = Array.isArray(failure.errors) && failure.errors.some(error =>
            typeof error.message === "string" && error.message.includes("manually blocked"));
          if (blocked) return json(503, { error: "AniList code exchange failed (HTTP 403, service blocked by AniList). The service owner needs to resolve this." });
          const reason = known.includes(failure.error) || errorName ? ", " + errorName :
            result.headers.get("cf-mitigated") === "challenge" ? ", provider challenge" :
            edgeCode ? ", edge " + edgeCode :
            responseText.includes("Just a moment") ? ", provider challenge page" :
            responseText.includes("Attention Required") ? ", provider access restriction" :
            responseText.includes("error code: 403") ? ", provider access restriction" :
            result.headers.get("Content-Type")?.includes("html") ? ", HTML error page" :
            /^error code: \d{3,4}\s*$/.test(responseText) ? ", " + responseText.trim() : "";
          return json(400, { error: `AniList code exchange failed (HTTP ${result.status}${reason}). Reconnect in mpv.` });
        }
        step = "AniList token response";
        const { access_token } = await result.json();
        if (typeof access_token !== "string" || !access_token) throw Error();
        return json(200, { access_token });
      }
      return json(404, {});
    } catch {
      return json(400, { error: `Sign-in failed at ${step}. Reconnect in mpv.` });
    }
  }
};
