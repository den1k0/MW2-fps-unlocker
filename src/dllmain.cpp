#include <windows.h>

#include "config.h"
#include "features.h"
#include "log.h"

#include <string>

namespace {

HMODULE g_self = nullptr;

std::wstring GetModuleDirectory(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring full(path, length);

    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::wstring(L".");
    }
    return full.substr(0, slash);
}

DWORD WINAPI WorkerThread(LPVOID /*unused*/) {
    const std::wstring directory = GetModuleDirectory(g_self);
    const std::wstring configPath = directory + L"\\unlocker.ini";
    const std::wstring logPath = directory + L"\\mw2_unlocker.log";

    mwlog::Open(logPath);
    mwlog::Line("MW2 (2009) x64 unlocker starting");
    mwlog::Line("config: %ls", configPath.c_str());

    Config config;
    if (!config.Load(configPath)) {
        mwlog::Line("WARNING: could not open config, using defaults");
    }

    const int delayMs = config.GetInt("general", "delayMs", 5000);
    const int toggleKey = config.GetInt("general", "toggleKey", 0x75); // F6
    // Some cvars are re-initialised by the engine after we write them - cg_fov
    // in particular when a level loads - so by default we keep checking and
    // re-apply anything the game has reset.
    const int keepApplied = config.GetInt("general", "keepApplied", 1);
    const int keepAliveMs = config.GetInt("general", "keepAliveMs", 2000);

    features::Init(configPath);

    if (delayMs > 0) {
        mwlog::Line("waiting %d ms before applying", delayMs);
        ::Sleep(static_cast<DWORD>(delayMs));
    }

    if (features::Apply()) {
        mwlog::Line("patches applied successfully");
    } else {
        mwlog::Line("apply failed: %s", features::LastError());
    }

    mwlog::Line("hotkey 0x%X toggles patches; DLL worker is running", toggleKey);
    if (keepApplied != 0) {
        mwlog::Line("keeping the values applied (checking every %d ms)", keepAliveMs);
    }

    bool previousDown = false;
    DWORD lastKeepAlive = ::GetTickCount();

    for (;;) {
        const bool down = (::GetAsyncKeyState(toggleKey) & 0x8000) != 0;
        if (down && !previousDown) {
            const bool state = features::Toggle();
            mwlog::Line("toggled -> %s", state ? "ON" : "OFF");
            lastKeepAlive = ::GetTickCount();
        }
        previousDown = down;

        if (keepApplied != 0 && keepAliveMs > 0) {
            const DWORD now = ::GetTickCount();
            if (now - lastKeepAlive >= static_cast<DWORD>(keepAliveMs)) {
                lastKeepAlive = now;
                features::KeepApplied();
            }
        }

        ::Sleep(30);
    }

    return 0; // unreachable: the worker runs for the lifetime of the process
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        ::DisableThreadLibraryCalls(module);

        // Do not do real work inside DllMain (loader lock). Hand off to a
        // worker thread instead.
        HANDLE thread = ::CreateThread(nullptr, 0, &WorkerThread, nullptr, 0, nullptr);
        if (thread != nullptr) {
            ::CloseHandle(thread);
        }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Exported API, for callers that prefer to drive the unlocker directly.
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) BOOL MW2U_Apply() {
    return features::Apply() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void MW2U_Restore() {
    features::Restore();
}

extern "C" __declspec(dllexport) BOOL MW2U_Toggle() {
    return features::Toggle() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL MW2U_IsApplied() {
    return features::Applied() ? TRUE : FALSE;
}
