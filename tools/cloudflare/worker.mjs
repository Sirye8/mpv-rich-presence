// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixed public entrance. The exchange and client credential live on Netlify.
const upstream = "https://mpv-anilist-login.netlify.app";
const routes = {"/health":"GET", "/start":"POST", "/callback":"GET", "/redeem":"POST"};

export default {
  async fetch(request) {
    const url = new URL(request.url);
    if (routes[url.pathname] !== request.method) return new Response(null, {status:404});
    if (Number(request.headers.get("Content-Length")) > 8192 || url.search.length > 8192)
      return new Response(null, {status:413});
    try {
      const target = new URL(url.pathname + url.search, upstream);
      const headers = new Headers({"User-Agent":"mpv-rich-presence/1.0",Accept:"application/json"});
      for (const key of ["Content-Type","Content-Length"])
        if (request.headers.has(key)) headers.set(key,request.headers.get(key));
      return await fetch(target, {
        method:request.method,headers,redirect:"manual",signal:AbortSignal.timeout(20000),
        body:request.method==="POST" ? request.body : undefined,
      });
    } catch {
      return Response.json({error:"The sign-in service is unavailable. Try Connect again."},
        {status:503,headers:{"Cache-Control":"no-store"}});
    }
  }
};
