#include <cookiejar.hpp>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace AnimepaheCLI
{
    namespace fs = std::filesystem;

    static bool kwikDebug()
    {
        const char *v = std::getenv("KWIK_DEBUG");
        return v && *v && *v != '0';
    }

    template <typename... Args>
    static void dbg(const char *fmtStr, Args &&...args)
    {
        if (!kwikDebug())
            return;
        std::fprintf(stderr, "[cookiejar] ");
        std::fprintf(stderr, fmtStr, std::forward<Args>(args)...);
        std::fprintf(stderr, "\n");
    }

    static std::vector<uint8_t> base64Decode(const std::string &in)
    {
        static const int8_t T[256] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
            -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
            -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};

        std::vector<uint8_t> out;
        out.reserve(in.size() * 3 / 4);
        int val = 0, valb = -8;
        for (unsigned char c : in)
        {
            if (c == '=' || T[c] < 0)
                continue;
            val = (val << 6) | T[c];
            valb += 6;
            if (valb >= 0)
            {
                out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    static std::vector<uint8_t> dpapiDecrypt(const std::vector<uint8_t> &blob)
    {
        DATA_BLOB in{static_cast<DWORD>(blob.size()), const_cast<BYTE *>(blob.data())};
        DATA_BLOB out{};
        if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        {
            return {};
        }
        std::vector<uint8_t> result(out.pbData, out.pbData + out.cbData);
        LocalFree(out.pbData);
        return result;
    }

    /* Read os_crypt.encrypted_key from Local State, base64-decode, strip 5-byte
     * "DPAPI" prefix, DPAPI-decrypt to get the 32-byte AES-GCM master key. */
    static std::vector<uint8_t> loadMasterKey(const fs::path &localStatePath)
    {
        std::ifstream f(localStatePath);
        if (!f)
            return {};
        nlohmann::json j;
        try
        {
            f >> j;
        }
        catch (...)
        {
            return {};
        }
        if (!j.contains("os_crypt") || !j["os_crypt"].contains("encrypted_key"))
            return {};

        std::string b64 = j["os_crypt"]["encrypted_key"].get<std::string>();
        auto raw = base64Decode(b64);
        if (raw.size() < 5)
            return {};

        const std::vector<uint8_t> prefix(raw.begin(), raw.begin() + 5);
        if (std::string(prefix.begin(), prefix.end()) != "DPAPI")
            return {};

        std::vector<uint8_t> encrypted(raw.begin() + 5, raw.end());
        return dpapiDecrypt(encrypted);
    }

    /* AES-256-GCM decrypt via Windows BCrypt. Returns empty on failure. */
    static std::string aesGcmDecrypt(const std::vector<uint8_t> &key,
                                     const uint8_t *nonce, ULONG nonceLen,
                                     const uint8_t *cipher, ULONG cipherLen,
                                     const uint8_t *tag, ULONG tagLen)
    {
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_KEY_HANDLE hKey = nullptr;
        std::string out;

        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0)
            return {};

        ULONG cbResult = 0;
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0)
        {
            BCryptCloseAlgorithmProvider(alg, 0);
            return {};
        }

        if (BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                                       const_cast<PUCHAR>(key.data()),
                                       static_cast<ULONG>(key.size()), 0) < 0)
        {
            BCryptCloseAlgorithmProvider(alg, 0);
            return {};
        }

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = const_cast<PUCHAR>(nonce);
        info.cbNonce = nonceLen;
        info.pbTag = const_cast<PUCHAR>(tag);
        info.cbTag = tagLen;

        std::vector<uint8_t> plain(cipherLen);
        NTSTATUS s = BCryptDecrypt(hKey,
                                   const_cast<PUCHAR>(cipher), cipherLen,
                                   &info, nullptr, 0,
                                   plain.data(), static_cast<ULONG>(plain.size()),
                                   &cbResult, 0);

        if (s == 0)
        {
            plain.resize(cbResult);
            out.assign(plain.begin(), plain.end());
        }

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(alg, 0);
        return out;
    }

    static std::string getEnvStr(const char *name)
    {
        const char *v = std::getenv(name);
        return (v && *v) ? std::string(v) : std::string();
    }

    /* Detect installed Chrome version from %LOCALAPPDATA%\Google\Chrome\Application\<version>
     * so the UA we send matches the browser session that issued cf_clearance. */
    static std::string detectChromeMajorVersion(const fs::path &browserRoot)
    {
        fs::path appDir = browserRoot / "Application";
        std::error_code ec;
        if (!fs::exists(appDir, ec))
            return {};
        std::string best;
        int bestMajor = 0;
        for (auto &e : fs::directory_iterator(appDir, ec))
        {
            if (!e.is_directory())
                continue;
            std::string name = e.path().filename().string();
            int major = 0;
            for (char c : name)
            {
                if (c == '.')
                    break;
                if (c < '0' || c > '9')
                {
                    major = 0;
                    break;
                }
                major = major * 10 + (c - '0');
            }
            if (major > bestMajor)
            {
                bestMajor = major;
                best = name;
            }
        }
        if (bestMajor == 0)
            return {};
        return std::to_string(bestMajor);
    }

    struct BrowserSlot
    {
        const char *displayName;
        fs::path root;       // %LOCALAPPDATA%\Google\Chrome\User Data
        fs::path appRoot;    // %LOCALAPPDATA%\Google\Chrome  (parent of "Application")
        const char *uaBrand; // appended to Chrome/x.0.0.0 UA, empty for Chrome
    };

    static std::vector<BrowserSlot> candidateBrowsers()
    {
        std::string local = getEnvStr("LOCALAPPDATA");
        if (local.empty())
            return {};
        fs::path localAppData(local);
        return {
            {"Chrome",
             localAppData / "Google" / "Chrome" / "User Data",
             localAppData / "Google" / "Chrome",
             ""},
            {"Edge",
             localAppData / "Microsoft" / "Edge" / "User Data",
             localAppData / "Microsoft" / "Edge",
             " Edg/"},
            {"Brave",
             localAppData / "BraveSoftware" / "Brave-Browser" / "User Data",
             localAppData / "BraveSoftware" / "Brave-Browser",
             ""},
        };
    }

    /* Copy the cookies SQLite file out to a temp location so we can read while the
     * browser may have it locked. Returns the temp path (caller deletes when done). */
    static fs::path snapshotCookiesDb(const fs::path &source)
    {
        std::error_code ec;
        if (!fs::exists(source, ec))
            return {};
        fs::path dst = fs::temp_directory_path(ec) /
                       fs::path("animepahe-cli-cookies-" +
                                std::to_string(GetCurrentProcessId()) + ".db");
        if (CopyFileW(source.wstring().c_str(), dst.wstring().c_str(), FALSE))
            return dst;
        return {};
    }

    static std::string buildCookieHeader(sqlite3 *db,
                                         const std::vector<uint8_t> &masterKey,
                                         const std::string &hostSuffix)
    {
        const char *sql =
            "SELECT name, encrypted_value, value FROM cookies "
            "WHERE host_key LIKE ?1 OR host_key = ?2";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return {};

        std::string likePattern = "%" + hostSuffix;
        std::string exactDot = "." + hostSuffix;
        sqlite3_bind_text(stmt, 1, likePattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, exactDot.c_str(), -1, SQLITE_TRANSIENT);

        std::string header;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char *nameRaw = sqlite3_column_text(stmt, 0);
            std::string name = nameRaw ? reinterpret_cast<const char *>(nameRaw) : "";
            int encLen = sqlite3_column_bytes(stmt, 1);
            const uint8_t *enc = static_cast<const uint8_t *>(sqlite3_column_blob(stmt, 1));

            std::string value;
            if (encLen >= 3 + 12 + 16 && enc &&
                (std::memcmp(enc, "v10", 3) == 0 || std::memcmp(enc, "v11", 3) == 0))
            {
                const uint8_t *nonce = enc + 3;
                const uint8_t *cipher = enc + 3 + 12;
                ULONG cipherLen = static_cast<ULONG>(encLen - 3 - 12 - 16);
                const uint8_t *tag = enc + encLen - 16;
                value = aesGcmDecrypt(masterKey, nonce, 12, cipher, cipherLen, tag, 16);
            }
            /* Chrome 127+ "v20" app-bound cookies require an elevated process and
             * the app-bound key — outside our scope. Fall back to plaintext value. */
            if (value.empty())
            {
                const unsigned char *plain = sqlite3_column_text(stmt, 2);
                if (plain)
                    value = reinterpret_cast<const char *>(plain);
            }

            if (name.empty() || value.empty())
                continue;

            if (!header.empty())
                header += "; ";
            header += name + "=" + value;
        }

        sqlite3_finalize(stmt);
        return header;
    }

    static bool tryBrowser(const BrowserSlot &slot,
                           const std::string &hostSuffix,
                           BrowserCookies &outResult)
    {
        std::error_code ec;
        if (!fs::exists(slot.root, ec))
        {
            dbg("%s: User Data dir missing (%s)", slot.displayName, slot.root.string().c_str());
            return false;
        }

        std::vector<uint8_t> masterKey = loadMasterKey(slot.root / "Local State");
        if (masterKey.empty())
        {
            dbg("%s: master key load failed (Local State missing or DPAPI decrypt failed)", slot.displayName);
            return false;
        }
        dbg("%s: master key loaded (%zu bytes)", slot.displayName, masterKey.size());

        /* Iterate Default + ProfileN until we find a profile that has the cookie. */
        std::vector<fs::path> profiles;
        profiles.push_back(slot.root / "Default");
        for (auto &e : fs::directory_iterator(slot.root, ec))
        {
            if (!e.is_directory())
                continue;
            std::string n = e.path().filename().string();
            if (n.rfind("Profile ", 0) == 0)
                profiles.push_back(e.path());
        }

        std::string targetCookie;
        for (const auto &profile : profiles)
        {
            fs::path src = profile / "Network" / "Cookies";
            if (!fs::exists(src, ec))
                src = profile / "Cookies";
            if (!fs::exists(src, ec))
            {
                dbg("%s: profile %s has no cookies file", slot.displayName,
                    profile.filename().string().c_str());
                continue;
            }
            dbg("%s: probing profile %s (%s)", slot.displayName,
                profile.filename().string().c_str(), src.string().c_str());

            fs::path snap = snapshotCookiesDb(src);
            if (snap.empty())
            {
                dbg("%s: CopyFileW failed (browser may hold an exclusive lock — try closing it)",
                    slot.displayName);
                continue;
            }

            sqlite3 *db = nullptr;
            if (sqlite3_open_v2(snap.string().c_str(), &db,
                                SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            {
                dbg("%s: sqlite3_open_v2 failed", slot.displayName);
                if (db)
                    sqlite3_close(db);
                fs::remove(snap, ec);
                continue;
            }

            std::string header = buildCookieHeader(db, masterKey, hostSuffix);
            dbg("%s: profile %s produced %zu byte cookie header (cf_clearance=%s)",
                slot.displayName, profile.filename().string().c_str(),
                header.size(),
                header.find("cf_clearance=") != std::string::npos ? "yes" : "no");
            sqlite3_close(db);
            fs::remove(snap, ec);

            /* Only accept profiles that produced cf_clearance — that's the
             * cookie that actually unblocks the request. */
            if (header.find("cf_clearance=") != std::string::npos)
            {
                targetCookie = std::move(header);
                break;
            }
            else if (targetCookie.empty() && !header.empty())
            {
                targetCookie = header; // keep partial match as fallback
            }
        }

        if (targetCookie.empty())
            return false;

        std::string major = detectChromeMajorVersion(slot.appRoot);
        if (major.empty())
            major = "138";

        std::string ua =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/" +
            major + ".0.0.0 Safari/537.36";
        if (slot.uaBrand && *slot.uaBrand)
            ua += std::string(slot.uaBrand) + major + ".0.0.0";

        outResult.cookieHeader = std::move(targetCookie);
        outResult.userAgent = std::move(ua);
        outResult.ok = true;
        return true;
    }

    BrowserCookies loadKwikCookiesFromBrowser(const std::string &hostSuffix)
    {
        BrowserCookies result;
        for (const auto &slot : candidateBrowsers())
        {
            if (tryBrowser(slot, hostSuffix, result))
                return result;
        }
        return result;
    }
}

#else /* !_WIN32 */

namespace AnimepaheCLI
{
    BrowserCookies loadKwikCookiesFromBrowser(const std::string &)
    {
        return {};
    }
}

#endif
