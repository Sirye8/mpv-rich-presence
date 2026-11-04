// SPDX-License-Identifier: AGPL-3.0-or-later
import test from "node:test";
import assert from "node:assert/strict";
import worker from "../tools/cloudflare/worker.mjs";

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
    assert.equal(req.body.get("code_verifier"),verifier);
    assert.equal(req.body.get("client_secret"),env.ANILIST_CLIENT_SECRET);
    assert.equal(req.body.get("code"),"example-code");
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
