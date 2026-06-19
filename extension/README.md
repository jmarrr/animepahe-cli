# animepahe-cli cookie helper (Chrome extension)

A tiny unpacked extension that keeps `cf_clearance` for **animepahe.\*** and
**kwik.cx** fresh and hands them off to `animepahe-cli` so you never set env
vars again.

## How it works

1. Watches cookies on every animepahe TLD it knows about (`.com`, `.org`,
   `.ru`, `.si`, `.pw`, `.tv`), plus `pahe.win` and `kwik.cx`.
2. Reads `cf_clearance` (and `__cf_bm` / `__cflb` when present) + your
   `navigator.userAgent`, writes them as a `cookiesByHost` map to
   `~/Downloads/animepahe-cli/cookie.json`.
3. Refreshes immediately on every `chrome.cookies.onChanged` event, plus a
   5-minute fallback alarm.
4. Every 25 minutes the extension silently re-visits each known site in a
   small off-screen popup so Cloudflare hands out a new clearance before the
   old one ages out — keeps the file warm without you having to remember.

The CLI's lookup order, per request host, is `KWIK_COOKIE` env var (kwik
only) → this file's `cookiesByHost` map → nothing.

## Install

1. Open `chrome://extensions` (or `edge://extensions`).
2. Toggle **Developer mode** on (top right).
3. Click **Load unpacked**.
4. Pick the `extension/` folder from this repo.
5. Visit https://animepahe.pw (or whichever TLD is currently live) **and**
   https://kwik.cx once. The extension picks up both `cf_clearance` cookies
   on the spot, writes the file, and keeps both fresh on its own from then on.

That's it. Run the CLI normally — no env vars.

## Verify

After step 5, check that the file exists:

```powershell
type "$env:USERPROFILE\Downloads\animepahe-cli\cookie.json"
```

You should see something like:

```json
{
  "cookiesByHost": {
    "kwik.cx": "cf_clearance=...; __cf_bm=...",
    "animepahe.pw": "cf_clearance=..."
  },
  "userAgent": "Mozilla/5.0 ...",
  "updatedAtMs": 1778401926123,
  "source": "animepahe-cli cookie helper"
}
```

If it doesn't appear:
- Make sure Chrome's Downloads folder isn't redirected somewhere unusual
  (Settings → Downloads → Location).
- Click the extension's toolbar icon to force an immediate refresh.
- `chrome://extensions` → the extension → **service worker** link to see logs.

## Optional: pin the toolbar icon

`chrome://extensions` → **Details** on this extension → toggle **Pin to
toolbar**. Clicking the icon forces an on-demand refresh — useful when you
know your cookie just expired and don't want to wait the next 5 minutes.

## What about Edge / Brave / other Chromium browsers?

Same install steps apply (`edge://extensions`, `brave://extensions`, …). The
file always lands under `%USERPROFILE%\Downloads\animepahe-cli\` regardless
of which browser hosts it.

## Privacy

The extension only reads cookies for `kwik.cx`, only writes to one specific
file in your Downloads folder, and makes no network calls of its own (the
silent kwik.cx visit goes through Chrome's normal navigation; Cloudflare and
kwik are the only servers contacted). Nothing leaves your machine.
