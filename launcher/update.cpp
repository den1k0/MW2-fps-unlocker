#include "update.h"

#include <windows.h>
#include <winhttp.h>

#include <string>

namespace {

// Where the published build number lives. Hard-coded rather than configurable: a
// version check that can be pointed somewhere else is a way to be told to
// download something that is not this project.
const wchar_t* const kHost = L"raw.githubusercontent.com";
const wchar_t* const kPath = L"/den1k0/MW2-fps-unlocker/main/version.txt";

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

        request = ::WinHttpOpenRequest(connect, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request == nullptr) {
            error = L"could not reach GitHub";
            break;
        }

        if (!::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
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
