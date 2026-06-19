/**
 * Reads cf_clearance from the user's Chrome for every domain animepahe-cli
 * needs to talk to (animepahe.*, pahe.win, kwik.cx) and writes them as one
 * JSON blob to Downloads/animepahe-cli/cookie.json. The CLI looks the cookie
 * up by request host at fetch time.
 *
 * Triggers:
 *   - On install / browser startup
 *   - chrome.cookies.onChanged for any watched host (instant sync)
 *   - 5-min chrome.alarms (catch-all)
 *   - 25-min silent visit to keep the cookie warm
 */

const HOSTS = [
  "kwik.cx",
  "pahe.win",
  "animepahe.com",
  "animepahe.org",
  "animepahe.ru",
  "animepahe.si",
  "animepahe.pw",
  "animepahe.tv",
];

/* Which sites are worth periodically re-visiting to refresh cf_clearance.
 * animepahe rotates TLDs, so we visit every known one — Cloudflare ignores
 * the visits to whichever TLDs aren't live. */
const REFRESH_VISIT_HOSTS = [
  "https://kwik.cx/",
  "https://animepahe.com/",
  "https://animepahe.org/",
  "https://animepahe.ru/",
  "https://animepahe.si/",
  "https://animepahe.pw/",
  "https://animepahe.tv/",
];

const OUT_RELATIVE_PATH = "animepahe-cli/cookie.json";
const REFRESH_PERIOD_MIN = 5;
const SILENT_VISIT_PERIOD_MIN = 25;

const INTERESTING_COOKIE_NAMES = new Set([
  "cf_clearance",
  "__cf_bm",
  "__cflb",
]);

function hostKeyFromDomain(domain) {
  /* Chrome stores domain cookies with a leading dot ".kwik.cx" — strip it. */
  return domain.startsWith(".") ? domain.slice(1) : domain;
}

async function getAllCookiesForHost(host) {
  /* chrome.cookies.getAll only returns unpartitioned cookies unless you also
   * pass partitionKey. Cloudflare now sets cf_clearance as a Partitioned
   * cookie (CHIPS), keyed to the site's own top-level origin. We have to ask
   * for both unpartitioned + the host's own partition explicitly. */
  const unpart = await chrome.cookies.getAll({ domain: host });
  let part = [];
  try {
    part = await chrome.cookies.getAll({
      domain: host,
      partitionKey: { topLevelSite: `https://${host}` },
    });
  } catch (e) {
    /* Older Chrome (<119) doesn't know partitionKey; skip silently. */
  }
  /* Dedupe by name — keep the partitioned variant when present, since
   * Cloudflare's current cf_clearance lives there. */
  const byName = new Map();
  for (const c of unpart) byName.set(c.name, c);
  for (const c of part) byName.set(c.name, c);
  return Array.from(byName.values());
}

async function collectCookiesByHost() {
  const byHost = {};
  for (const host of HOSTS) {
    let cookies;
    try {
      cookies = await getAllCookiesForHost(host);
    } catch (e) {
      console.error(`[helper] getAll(${host}) failed:`, e);
      continue;
    }
    if (!cookies || !cookies.length) continue;

    const pairs = cookies
      .filter((c) => INTERESTING_COOKIE_NAMES.has(c.name))
      .map((c) => `${c.name}=${c.value}`);
    if (pairs.length) {
      byHost[host] = pairs.join("; ");
    }
  }
  return byHost;
}

async function writePayload(payload) {
  const json = JSON.stringify(payload, null, 2);
  const b64 = btoa(unescape(encodeURIComponent(json)));
  const dataUrl = `data:application/json;base64,${b64}`;
  return chrome.downloads.download({
    url: dataUrl,
    filename: OUT_RELATIVE_PATH,
    conflictAction: "overwrite",
    saveAs: false,
  });
}

chrome.downloads.onChanged.addListener((delta) => {
  if (delta.state && delta.state.current === "complete") {
    chrome.downloads.search({ id: delta.id }, (results) => {
      if (
        results[0] &&
        results[0].filename &&
        results[0].filename.endsWith("cookie.json")
      ) {
        chrome.downloads.erase({ id: delta.id });
      }
    });
  }
});

async function refresh() {
  try {
    const byHost = await collectCookiesByHost();
    const knownHosts = Object.keys(byHost);
    if (knownHosts.length === 0) {
      console.log(
        "[helper] no cf_clearance for any watched host yet — visit animepahe and kwik.cx once"
      );
      return false;
    }
    const payload = {
      cookiesByHost: byHost,
      userAgent: self.navigator.userAgent,
      updatedAtMs: Date.now(),
      source: "animepahe-cli cookie helper",
    };
    await writePayload(payload);
    console.log("[helper] wrote cookie file for hosts:", knownHosts.join(", "));
    return true;
  } catch (e) {
    console.error("[helper] refresh failed:", e);
    return false;
  }
}

/* Open each host in a minimized popup so Cloudflare re-issues cf_clearance
 * without stealing focus. (Off-screen positioning is rejected by Chrome —
 * windows must be at least 50% on a visible monitor — so we minimize instead.) */
async function silentVisit() {
  for (const url of REFRESH_VISIT_HOSTS) {
    let win;
    try {
      win = await chrome.windows.create({
        url,
        type: "popup",
        state: "minimized",
        focused: false,
      });
      await new Promise((r) => setTimeout(r, 5000));
    } catch (e) {
      console.error(`[helper] silentVisit(${url}) failed:`, e);
    } finally {
      if (win && win.id != null) {
        try {
          await chrome.windows.remove(win.id);
        } catch {}
      }
    }
  }
  await refresh();
}

chrome.runtime.onInstalled.addListener(() => {
  chrome.alarms.create("refresh", { periodInMinutes: REFRESH_PERIOD_MIN });
  chrome.alarms.create("silent-visit", {
    periodInMinutes: SILENT_VISIT_PERIOD_MIN,
  });
  refresh();
});

chrome.runtime.onStartup.addListener(() => {
  refresh();
});

chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === "refresh") refresh();
  if (alarm.name === "silent-visit") silentVisit();
});

chrome.cookies.onChanged.addListener((change) => {
  if (change.removed) return;
  if (!INTERESTING_COOKIE_NAMES.has(change.cookie.name)) return;
  const host = hostKeyFromDomain(change.cookie.domain);
  if (HOSTS.some((h) => host === h || host.endsWith("." + h) || h.endsWith("." + host))) {
    refresh();
  }
});

chrome.action.onClicked.addListener(() => {
  silentVisit();
});
