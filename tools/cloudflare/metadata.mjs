// SPDX-License-Identifier: AGPL-3.0-or-later
const tmdb = "https://api.themoviedb.org/3";
const noStore = { "Cache-Control": "no-store" };

function movieFields(item) {
  return {
    id: item.id, title: item.title, name: item.name,
    release_date: item.release_date, first_air_date: item.first_air_date,
    poster_path: item.poster_path,
    genres: item.genres?.map(({ name }) => ({ name })),
  };
}

export async function metadata(request, url, env) {
  if (request.method !== "GET") return new Response(null, { status: 405, headers: noStore });
  const token = env.TMDB_READ_TOKEN;
  const path = url.pathname.slice("/metadata".length);
  if (path === "/health" && !url.search) {
    return Response.json({ configured: Boolean(token) }, { headers: noStore });
  }
  const search = path.match(/^\/search\/(movie|tv)$/);
  const details = /^\/(movie|tv)\/[1-9]\d{0,9}$/.test(path)
    || /^\/tv\/[1-9]\d{0,9}\/season\/\d{1,2}\/episode\/[1-9]\d{0,2}$/.test(path);
  if (!search && !details) return new Response(null, { status: 404, headers: noStore });
  if (url.search.length > 1200 || (details && url.search)) {
    return new Response(null, { status: 400, headers: noStore });
  }
  const target = new URL(tmdb + path);
  if (search) {
    const yearKey = search[1] === "movie" ? "year" : "first_air_date_year";
    const query = url.searchParams.get("query")?.trim() ?? "";
    const year = url.searchParams.get(yearKey);
    if (!query || query.length > 180 || /[\\/?&#\x00-\x1f]|:\/\//u.test(query)
        || (year !== null && !/^(18|19|20)\d{2}$/.test(year))
        || Array.from(url.searchParams.keys()).some(key => key !== "query" && key !== yearKey)
        || url.searchParams.getAll("query").length !== 1
        || url.searchParams.getAll(yearKey).length > 1) {
      return new Response(null, { status: 400, headers: noStore });
    }
    target.searchParams.set("query", query);
    if (year) target.searchParams.set(yearKey, year);
  }
  if (!token) return new Response(null, { status: 503, headers: noStore });

  try {
    const client = request.headers.get("CF-Connecting-IP") || "unknown";
    if (!(await env.TMDB_LIMIT.limit({ key: client })).success) {
      return new Response(null, { status: 429, headers: { ...noStore, "Retry-After": "60" } });
    }
    const result = await fetch(target, {
      headers: {
        Authorization: "Bearer " + token,
        Accept: "application/json",
        "User-Agent": "mpv-rich-presence/1.0 (https://github.com/Sirye8/mpv-rich-presence)",
      },
      redirect: "manual",
      signal: AbortSignal.timeout(6000),
    });
    if (result.ok && result.headers.get("Content-Type")?.includes("application/json")) {
      const data = await result.json();
      const output = search ? { results: data.results.slice(0, 10).map(movieFields) }
        : path.includes("/episode/") ? { name: data.name }
        : movieFields(data);
      return Response.json(output, { headers: noStore });
    }
    if (result.status === 404) return new Response(null, { status: 404, headers: noStore });
  } catch {
    // Keep the fallback available even if the service or limiter is down.
  }
  return new Response(null, { status: 503, headers: noStore });
}
