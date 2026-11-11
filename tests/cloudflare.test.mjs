// SPDX-License-Identifier: AGPL-3.0-or-later
import test from "node:test";
import assert from "node:assert/strict";
import worker from "../tools/login/exchange.mjs";
import relay from "../tools/cloudflare/worker.mjs";

test("TMDB relay accepts only public metadata requests and keeps the credential server-side", async t => {
  const root = "https://login.example.org";
  const request = path => new Request(root + path, { headers: { Cookie: "private", Authorization: "private" } });
  const env = { TMDB_READ_TOKEN: "test-token", TMDB_LIMIT: { limit: async () => ({ success: true }) } };
  assert.deepEqual(await (await relay.fetch(request("/metadata/health"))).json(), { configured: false });
  assert.equal((await relay.fetch(request("/metadata/search/movie?query=Interstellar"))).status, 503);
  const upstream = t.mock.method(globalThis, "fetch", async (target, options) => {
    assert.equal(target.href, "https://api.themoviedb.org/3/search/movie?query=Interstellar&year=2014");
    assert.equal(options.headers.Authorization, "Bearer test-token");
    assert.equal(options.headers.Cookie, undefined);
    assert.equal(options.redirect, "manual");
    return Response.json({ results: [{ id: 157336, title: "Interstellar", overview: "Unused data" }] });
  });
  for (const path of ["/metadata/account", "/metadata/search/movie?query=%2Fprivate%2Fpath",
    "/metadata/search/movie?query=Interstellar&append_to_response=account",
    "/metadata/search/movie?query=One&query=Two", "/metadata/movie/12?api_key=bad"]) {
    assert.ok((await relay.fetch(request(path), env)).status >= 400);
  }
  assert.equal(upstream.mock.callCount(), 0);
  const response = await relay.fetch(request("/metadata/search/movie?query=Interstellar&year=2014"), env);
  assert.deepEqual(await response.json(), { results: [{ id: 157336, title: "Interstellar" }] });
  assert.equal(response.headers.get("Cache-Control"), "no-store");
  env.TMDB_LIMIT.limit = async () => ({ success: false });
  assert.equal((await relay.fetch(request("/metadata/search/movie?query=Interstellar"), env)).status, 429);
  assert.equal(upstream.mock.callCount(), 1);
});

test("a viewer exchanges one code with the original verifier", async t => {
  const origin = "https://login.example.org";
  const env = { ANILIST_CLIENT_ID: "55822", ANILIST_CLIENT_SECRET: "test-only" };
  const verifier = "v".repeat(64);
  const state = "s".repeat(32);
  const challenge = Buffer.from(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier))).toString("base64url");
  function post(path, fields) {
    const body = JSON.stringify(fields);
    return worker.fetch(new Request(origin + path, { method: "POST", body, headers: { "Content-Length": String(Buffer.byteLength(body)) } }), env);
  }
  const start = await (await post("/start", { challenge, state })).json();
  const params = new URL(start.authorize).searchParams;
  assert.equal(params.get("code_challenge"), challenge);
  assert.equal(params.get("redirect_uri"), origin + "/callback");
  const callback = origin + "/callback?code=example-code&state=" + params.get("state");
  const response = await worker.fetch(new Request(callback), env);
  assert.equal(response.status,303);
  const location = new URL(response.headers.get("Location"));
  assert.equal(location.origin, "http://127.0.0.1:47653");
  assert.equal(location.searchParams.get("state"),state);
  assert.ok(!location.href.includes("example-code"));
  const ticket = location.searchParams.get("code");
  let used=false;
  const provider=t.mock.method(globalThis,"fetch",async (url, req) => {
    assert.equal(url,"https://anilist.co/api/v2/oauth/token");
    assert.equal(req.redirect,"manual");
    const body = JSON.parse(req.body);
    assert.equal(body.code_verifier,verifier);
    assert.equal(body.client_secret,env.ANILIST_CLIENT_SECRET);
    assert.equal(body.code,"example-code");
    if (used) return Response.json({}, {status:400});
    used=true;
    return Response.json({access_token:"viewer-token"});
  });
  assert.equal((await post("/redeem",{ticket,verifier:"x".repeat(64)})).status,400);
  assert.equal(provider.mock.callCount(),0);
  assert.deepEqual(await (await post("/redeem",{ticket,verifier})).json(), {access_token:"viewer-token"});
  assert.equal((await post("/redeem",{ticket,verifier})).status,400);
  t.mock.method(Date,"now",()=>new Date().getTime()+300000);
  assert.equal((await worker.fetch(new Request(callback),env)).status,400);
  assert.equal((await post("/redeem",{ticket,verifier})).status,400);
  assert.equal(provider.mock.callCount(),2);
});

test("the relay forwards only sign-in routes without following the callback", async t => {
  const origin = "https://mpv-anilist-login.si-based.workers.dev";
  const provider = t.mock.method(globalThis, "fetch", async (url, req) => {
    assert.equal(url.origin, "https://mpv-anilist-login.netlify.app");
    assert.equal(req.redirect, "manual");
    assert.equal(req.headers.has("Cookie"), false);
    assert.equal(req.headers.has("Authorization"), false);
    if (url.pathname === "/callback") {
      assert.equal(url.searchParams.get("state"), "opaque-state");
      return new Response(null, { status: 303, headers: {
        Location: "http://127.0.0.1:47653/callback?code=encrypted-ticket",
        "Cache-Control": "no-store",
      } });
    }
    assert.equal(url.pathname, "/start");
    assert.equal(await new Response(req.body).text(), "{}");
    assert.equal(req.headers.get("Content-Length"), "2");
    return Response.json({ authorize: "https://anilist.co/api/v2/oauth/authorize" });
  });
  for (const path of ["/", "/probe", "/https://example.org", "//example.org"]) {
    assert.equal((await relay.fetch(new Request(origin + path))).status, 404);
  }
  assert.equal((await relay.fetch(new Request(origin + "/redeem"))).status, 404);
  const start = await relay.fetch(new Request(origin + "/start", {
    method: "POST", body: "{}", headers: { "Content-Length": "2", Cookie: "private", Authorization: "private" },
  }));
  assert.equal(start.status, 200);
  const callback = await relay.fetch(new Request(origin + "/callback?state=opaque-state"));
  assert.equal(callback.status, 303);
  assert.equal(new URL(callback.headers.get("Location")).hostname, "127.0.0.1");
  assert.equal(callback.headers.get("Cache-Control"), "no-store");
  assert.equal(provider.mock.callCount(), 2);
  provider.mock.mockImplementation(() => { throw Error("network failure"); });
  assert.equal((await relay.fetch(new Request(origin + "/health"))).status, 503);
});

test("the exchange uses its public relay origin for authorization and health", async () => {
  const env = { ANILIST_CLIENT_ID: "55822", ANILIST_CLIENT_SECRET: "test-only",
    ANILIST_PUBLIC_ORIGIN: "https://relay.example.org" };
  const origin = "https://exchange.example.org";
  const health = await (await worker.fetch(new Request(origin + "/health"), env)).json();
  assert.equal(health.redirect_uri, env.ANILIST_PUBLIC_ORIGIN + "/callback");
  const body = JSON.stringify({ state: "s".repeat(32), challenge: "c".repeat(43) });
  const result = await (await worker.fetch(new Request(origin + "/start", {
    method: "POST", body, headers: { "Content-Length": String(body.length) },
  }), env)).json();
  assert.equal(new URL(result.authorize).searchParams.get("redirect_uri"), health.redirect_uri);
  assert.equal(JSON.stringify(result).includes(env.ANILIST_CLIENT_SECRET), false);
});
