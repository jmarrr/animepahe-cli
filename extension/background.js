/**
 * Reads cf_clearance + UA from the user's running Chrome and writes it to
 * Downloads/animepahe-cli/cookie.json, where animepahe-cli reads it from.
 *
 * Triggers:
 *   - On install / browser startup
 *   - Every 5 minutes via chrome.alarms
 *   - Whenever kwik.cx sets/updates the cf_clearance cookie
 *
 * Approach: use chrome.downloads with a data: URL + conflictAction:"overwrite",
 * then erase the resulting download record so the user's history stays clean.
 * Native messaging would be cleaner but requires a registered native host;
 * downloads-based file drop ships as a pure-JS extension with no installer.
 */

const KWIK_URL = "https://kwik.cx/";
const COOKIE_NAME = "cf_clearance";
const OUT_RELATIVE_PATH = "animepahe-cli/cookie.json";
const REFRESH_PERIOD_MIN = 5;
const REFRESH_TAB_VISIT_MIN = 25; // re-visit kwik.cx silently before cookie ages out

async function getKwikClearance() {
  return chrome.cookies.get({ url: KWIK_URL, name: COOKIE_NAME });
}

async function writeCookieFile(cookie) {
  const payload = {
    cookie: `${COOKIE_NAME}=${cookie.value}`,
    userAgent: self.navigator.userAgent,
    expiresAtMs: cookie.expirationDate
      ? Math.round(cookie.expirationDate * 1000)
      : null,
    updatedAtMs: Date.now(),
    source: "animepahe-cli cookie helper",
  };

  /* base64-encode JSON into a data: URL — chrome.downloads.download only
   * accepts http/https/data, not raw text. */
  const json = JSON.stringify(payload, null, 2);
  const b64 = btoa(unescape(encodeURIComponent(json)));
  const dataUrl = `data:application/json;base64,${b64}`;

  const id = await chrome.downloads.download({
    url: dataUrl,
    filename: OUT_RELATIVE_PATH,
    conflictAction: "overwrite",
    saveAs: false,
  });
  return id;
}

/* Erase the download record once it finishes so the user's history doesn't
 * fill up with cookie.json entries. */
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
    const cookie = await getKwikClearance();
    if (!cookie || !cookie.value) {
      console.log("[helper] no cf_clearance present — open kwik.cx once");
      return false;
    }
    await writeCookieFile(cookie);
    console.log("[helper] wrote cookie file (expires", cookie.expirationDate, ")");
    return true;
  } catch (e) {
    console.error("[helper] refresh failed:", e);
    return false;
  }
}

/* Silently visit kwik.cx so Cloudflare re-issues cf_clearance before the
 * current one ages out. Opens a tiny off-screen popup window that's gone
 * within a few seconds. */
async function silentVisit() {
  let win;
  try {
    win = await chrome.windows.create({
      url: KWIK_URL,
      type: "popup",
      width: 320,
      height: 200,
      left: -32000,
      top: -32000,
      focused: false,
    });
    /* Give Cloudflare time to issue the challenge + Chrome to write Set-Cookie. */
    await new Promise((r) => setTimeout(r, 7000));
  } catch (e) {
    console.error("[helper] silentVisit failed:", e);
  } finally {
    if (win && win.id != null) {
      try {
        await chrome.windows.remove(win.id);
      } catch {}
    }
  }
  await refresh();
}

chrome.runtime.onInstalled.addListener(() => {
  chrome.alarms.create("refresh", { periodInMinutes: REFRESH_PERIOD_MIN });
  chrome.alarms.create("silent-visit", {
    periodInMinutes: REFRESH_TAB_VISIT_MIN,
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

/* Pick up cookie changes the moment Chrome receives a fresh Set-Cookie from
 * kwik.cx — keeps the file in sync without waiting for the 5-min alarm. */
chrome.cookies.onChanged.addListener((change) => {
  if (
    !change.removed &&
    change.cookie &&
    change.cookie.name === COOKIE_NAME &&
    change.cookie.domain.endsWith("kwik.cx")
  ) {
    refresh();
  }
});

/* Click the toolbar icon to force a refresh + visit on demand. */
chrome.action.onClicked.addListener(() => {
  silentVisit();
});
