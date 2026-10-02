#include "update.h"

#include <windows.h>
#include <winhttp.h>

#include <string>

namespace {

// Where the published build number lives. Hard-coded rather than configurable: a
// version check that can be pointed somewhere else is a way to be told to
// download something that is not this project.
//
// The API host rather than the raw one, which is the obvious choice and the wrong
// one: raw.githubusercontent.com is a CDN that holds every response for five
// minutes and ignores a cache-busting query string (measured - "X-Cache: HIT",
// with the same stale body, for a URL that had never been requested). A check
// made just after a release therefore reported the previous number, which is
// indistinguishable from a broken button.
//
// The contents API is better but not instant either: its answers carry
// "Cache-Control: public, max-age=60, s-maxage=60", so the number can be up to a
// minute behind a release. That is the honest figure - a minute is
// indistinguishable from "now" for anyone pressing a button, and five minutes of
// "up to date" is not.
//
// It is also not unlimited: an unauthenticated client gets 60 requests an hour
// per address, which is far more than anyone pressing this button will use - and
// 403, which is what running out looks like, is reported as "try again later"
// rather than as a failure.
const wchar_t* const kHost = L"api.github.com";
const wchar_t* const kPath = L"/repos/den1k0/MW2-fps-unlocker/contents/version.txt";

// Nobody should wait on a button for longer than this. One small text file, so
// five seconds per stage is generous rather than tight.
const int kTimeoutMs = 5000;

// The first run of digits in the file. version.txt is one number on one line,
// but there is no reason to be strict about whitespace or a trailing newline.
// Returns -1 when there is no digit at all.
int FirstNumber(const std::string& text) {
    int value = -1;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            value = (value < 0 ? 0 : value) * 10 + (c - '0');
        } else if (value >= 0) {
            break;
        }
    }
    return value;
}

} // namespace

const wchar_t* update::DownloadPage() {
    return L"https://github.com/den1k0/MW2-fps-unlocker";
}

int update::LatestBuild(std::wstring& error) {
    error.clear();

    // The default access type rather than the automatic one: this works on every
    // Windows that can run the game, and it still honours the proxy settings
    // WinHTTP is given by the system.
    HINTERNET session = ::WinHttpOpen(L"MW2Unlocker", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error = L"could not start the download";
        return -1;
    }
    ::WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    int result = -1;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    do {
        connect = ::WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connect == nullptr) {
            error = L"could not reach GitHub";
            break;
        }

        // The path used to carry a cache-busting query string. It is gone: the
        // CDN ignored it, so it only made the request look clever. What keeps the
        // answer current is the API host above and these two headers - the media
        // type that asks for the file's contents rather than its JSON description,
        // and the User-Agent GitHub wants on an API call.
        request = ::WinHttpOpenRequest(connect, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
        if (request == nullptr) {
            error = L"could not reach GitHub";
            break;
        }

        if (!::WinHttpSendRequest(request,
                                  L"Accept: application/vnd.github.raw\r\n"
                                  L"User-Agent: MW2Unlocker\r\n"
                                  L"Cache-Control: no-cache\r\n",
                                  static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !::WinHttpReceiveResponse(request, nullptr)) {
            error = L"could not reach GitHub";
            break;
        }

        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                   WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                   WINHTTP_NO_HEADER_INDEX)) {
            error = L"could not reach GitHub";
            break;
        }
        // 404 is the ordinary state of a repository that has not published the
        // file yet, so it gets its own wording rather than looking like a fault.
        if (status == 404) {
            error = L"no release published yet";
            break;
        }
        // 403 is the API's rate limit, which is per address and shared with
        // anything else on it. Saying so is more useful than "GitHub answered
        // 403", and it is not a fault in this tool.
        if (status == 403 || status == 429) {
            error = L"GitHub is busy; try again later";
            break;
        }
        if (status != 200) {
            error = L"GitHub answered " + std::to_wstring(status);
            break;
        }

        std::string body;
        for (;;) {
            DWORD available = 0;
            if (!::WinHttpQueryDataAvailable(request, &available) || available == 0) {
                break;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!::WinHttpReadData(request, chunk.data(), available, &read)) {
                break;
            }
            chunk.resize(read);
            body += chunk;
            // One number on one line is all that is expected; anything longer is
            // not this file.
            if (body.size() > 256) {
                break;
            }
        }

        const int published = FirstNumber(body);
        if (published < 0) {
            error = L"the version file has no number";
            break;
        }
        result = published;
    } while (false);

    if (request != nullptr) {
        ::WinHttpCloseHandle(request);
    }
    if (connect != nullptr) {
        ::WinHttpCloseHandle(connect);
    }
    ::WinHttpCloseHandle(session);
    return result;
}
