#pragma once

#ifndef COOKIEJAR_HPP
#define COOKIEJAR_HPP

#include <string>

namespace AnimepaheCLI
{
    struct BrowserCookies
    {
        std::string cookieHeader; // semicolon-joined "name=value; name2=value2"
        std::string userAgent;    // best-effort UA tied to the cookies (currently the most recent Chrome stable on the host)
        bool ok = false;
    };

    /* Pull cf_clearance (and any other host cookies) for a given Cloudflare-protected
     * domain out of a locally-installed Chromium-based browser's cookie store.
     * Returns ok=false silently when the browser isn't installed, the cookie isn't
     * present, or decryption fails — caller should fall back to env-var overrides.
     * Windows only (no-op on other platforms — header is included on all builds). */
    BrowserCookies loadKwikCookiesFromBrowser(const std::string &hostSuffix);
}

#endif
