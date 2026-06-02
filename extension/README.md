# animepahe-cli cookie helper (Chrome extension)

A tiny unpacked extension that keeps `cf_clearance` for `kwik.cx` fresh and
hands it off to `animepahe-cli` so you never set env vars again.

## How it works

1. Reads `cf_clearance` + `navigator.userAgent` from your running Chrome.
2. Writes them to `~/Downloads/animepahe-cli/cookie.json` (the CLI looks there).
3. Refreshes every 5 minutes, and silently re-visits `kwik.cx` every 25 minutes
   so Cloudflare hands out a new clearance before the old one ages out.

The CLI's lookup order is `KWIK_COOKIE` env var → this file → nothing. The
extension doesn't replace the env-var path — it just removes the need for it.

## Install

1. Open `chrome://extensions` (or `edge://extensions`).
2. Toggle **Developer mode** on (top right).
3. Click **Load unpacked**.
4. Pick the `extension/` folder from this repo.
5. Visit https://kwik.cx once so Cloudflare drops a `cf_clearance` cookie.
   The extension picks it up on cookie change, writes the file, and then
   keeps it fresh on its own.

That's it. Run the CLI normally — no env vars.

## Verify

After step 5, check that the file exists:

```powershell
type "$env:USERPROFILE\Downloads\animepahe-cli\cookie.json"
```

You should see something like:

```json
{
  "cookie": "cf_clearance=...",
  "userAgent": "Mozilla/5.0 ...",
  "expiresAtMs": 1780401926000,
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
