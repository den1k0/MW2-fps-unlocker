#include <windows.h>

#include "config.h"
#include "features.h"
#include "ipc.h"
#include "log.h"

#include <cstring>
#include <string>

namespace {

HMODULE g_self = nullptr;

// The in-game hotkey. Written by the control window and read by the worker loop,
// and both of those run on this thread, so it needs no synchronisation.
int g_toggleKey = 0x75; // F6

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

// -----------------------------------------------------------------------------
// The live control window.
//
// A window is the simplest cross-process channel Windows offers that needs no
// agreement on pipes, ports or file names: the launcher finds this by class name
// and sends WM_COPYDATA, which the kernel marshals into this process. Because the
// message is delivered to the thread that created the window, the worker loop
// below has to pump it.
// -----------------------------------------------------------------------------

// A fault inside a window procedure is escalated by the kernel to
// STATUS_FATAL_USER_CALLBACK_EXCEPTION and takes the whole process with it. Here
// that process is the player's game, so the body is guarded: our own bug may cost
// one rejected update, never the session. Measured the hard way - an earlier
// version that sent a custom message instead of WM_COPYDATA killed the game on
// the first Apply.
bool ApplyLiveGuarded(const ipc::Values* values) {
    __try {
        return features::ApplyLive(*values);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

LRESULT CALLBACK LiveWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
        if (data == nullptr || data->lpData == nullptr ||
            data->dwData != ipc::kCopyDataMagic) {
            return 0;
        }
        if (data->cbData != sizeof(ipc::Values)) {
            mwlog::Line("ipc: rejected a live update of %lu bytes (expected %zu)",
                        static_cast<unsigned long>(data->cbData), sizeof(ipc::Values));
            return 0;
        }

        // Copied out before use. The kernel marshalled the payload into this
        // process for the duration of the call, but it is still the sender's
        // memory, so nothing here should hold on to it.
        ipc::Values values{};
        std::memcpy(&values, data->lpData, sizeof(values));

        // The hotkey is carried in the same message even though features::ApplyLive
        // has no use for it: this is the thread that polls for it.
        if (values.toggleKey >= 0x70 && values.toggleKey <= 0x7B &&
            values.toggleKey != g_toggleKey) {
            g_toggleKey = values.toggleKey;
            mwlog::Line("ipc: toggle key is now 0x%X (F%d)", g_toggleKey, g_toggleKey - 0x70 + 1);
        }

        const bool changed = ApplyLiveGuarded(&values);
        mwlog::Line("ipc: live update %s", changed ? "applied" : "made no change");
        return changed ? 1 : 0;
    }

    return ::DefWindowProcW(window, message, wparam, lparam);
}

// A hidden top-level window, deliberately not a message-only one: message-only
// windows cannot be found by FindWindow, which is exactly how the launcher
// locates this. It is never shown, so it stays out of the taskbar and alt-tab.
HWND CreateLiveWindow() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &LiveWindowProc;
    windowClass.hInstance = g_self;
    windowClass.lpszClassName = ipc::kWindowClass;

    if (::RegisterClassExW(&windowClass) == 0 &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        mwlog::Line("ipc: could not register the control window class (error %lu)",
                    ::GetLastError());
        return nullptr;
    }

    const HWND window = ::CreateWindowExW(0, ipc::kWindowClass, L"MW2Unlocker", 0, 0, 0, 0, 0,
                                          nullptr, nullptr, g_self, nullptr);
    if (window == nullptr) {
        mwlog::Line("ipc: could not create the control window (error %lu)", ::GetLastError());
    }
    return window;
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
    g_toggleKey = config.GetInt("general", "toggleKey", 0x75); // F6
    // Some cvars are re-initialised by the engine after we write them - cg_fov
    // in particular when a level loads or the player respawns - so by default we
    // keep checking and re-apply anything the game has reset.
    //
    // The interval is deliberately short: a reset is visible as the FOV
    // snapping back to its default until the next check, so this trades a
    // negligible amount of work (a few 4-byte reads of our own memory) for a
    // gap short enough not to see. At 2000 ms a respawn was visibly wrong for
    // two seconds.
    const int keepApplied = config.GetInt("general", "keepApplied", 1);
    const int keepAliveMs = config.GetInt("general", "keepAliveMs", 100);

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

    mwlog::Line("hotkey 0x%X toggles patches; DLL worker is running", g_toggleKey);
    if (keepApplied != 0) {
        mwlog::Line("keeping the values applied (checking every %d ms)", keepAliveMs);
    }

    // Created here, after the first apply, rather than straight away: a value
    // pushed while the config was still being applied would be thrown away by
    // that apply, and at that moment the config is what should win.
    if (CreateLiveWindow() != nullptr) {
        mwlog::Line("ipc: live control window ready ('%ls') - the launcher can change values "
                    "without a restart", ipc::kWindowClass);
    }

    bool previousDown = false;
    DWORD lastKeepAlive = ::GetTickCount();

    for (;;) {
        // The control window lives on this thread, so its messages have to be
        // pumped here; a cross-process SendMessage blocks until they are.
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }

        const bool down = (::GetAsyncKeyState(g_toggleKey) & 0x8000) != 0;
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
