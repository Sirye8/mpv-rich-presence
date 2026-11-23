// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixed public entrance. The exchange and client credential live on Netlify.
import { metadata } from "./metadata.mjs";

const upstream = "https://mpv-anilist-login.netlify.app";
const routes = {"/health":"GET", "/start":"POST", "/callback":"GET", "/redeem":"POST"};

// Counts requests per route and status only. Titles, IDs, tokens and addresses are never written.
function routeName(path) {
  if (routes[path]) return path;
  const relay = path.match(/^\/metadata\/(health|search|movie|tv)(\/\d+)?(\/season\/\d+\/episode\/\d+)?/);
  if (!relay) return "other";
  return "/metadata/" + (relay[1] === "tv" && relay[3] ? "episode" : relay[1] === "movie" || relay[1] === "tv" ? "details" : relay[1]);
}

function count(env, path, status) {
  try { env.REQUESTS?.writeDataPoint({ blobs: [routeName(path), String(status)], indexes: [routeName(path)] }); }
  catch { /* Counting never affects the answer. */ }
}

export default {
  async fetch(request, env = {}) {
    const response = await answer(request, env);
    count(env, new URL(request.url).pathname, response.status);
    return response;
  }
};

async function answer(request, env) {
  {
    const url = new URL(request.url);
    if (url.pathname.startsWith("/metadata/")) return metadata(request, url, env);
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
}
