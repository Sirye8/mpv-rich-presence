// SPDX-License-Identifier: AGPL-3.0-or-later
import exchange from "../login/exchange.mjs";

export default function(request) {
  return exchange.fetch(request, {
    ANILIST_CLIENT_ID: process.env.ANILIST_CLIENT_ID || "55822",
    ANILIST_CLIENT_SECRET: process.env.ANILIST_CLIENT_SECRET,
    ANILIST_PUBLIC_ORIGIN: "https://mpv-anilist-login.si-based.workers.dev",
  });
}

export const config = { path: ["/health", "/start", "/callback", "/redeem"] };
