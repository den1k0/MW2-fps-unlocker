// -----------------------------------------------------------------------------
// x64 LoadLibrary injector for mw2_unlocker.dll.
//
// This must be built as 64-bit. A 32-bit injector cannot open a 64-bit game
// process (CreateRemoteThread / WriteProcessMemory across the WOW64 boundary
// fail), which is exactly why every old 32-bit "fps unlocker" stopped working.
// -----------------------------------------------------------------------------
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

namespace {

DWORD FindProcessIdByName(const std::wstring& name) {
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (::Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }

    ::CloseHandle(snapshot);
    return pid;
}

bool IsNumeric(const std::wstring& text) {
    if (text.empty()) {
        return false;
    }
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return false;
        }
    }
    return true;
}

std::wstring AbsolutePath(const std::wstring& path) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = ::GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr);
    if (length == 0 || length >= MAX_PATH) {
        return path;
    }
    return std::wstring(buffer, length);
}

int Fail(const wchar_t* message) {
    std::fwprintf(stderr, L"error: %ls (GetLastError=%lu)\n", message, ::GetLastError());
    return 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::fwprintf(stderr,
                      L"usage: injector <exe-name|pid> <dll-path>\n"
                      L"   ex: injector iw4sp.exe \"C:\\path\\mw2_unlocker.dll\"\n");
        return 1;
    }

    const std::wstring target = argv[1];
    const std::wstring dllPath = AbsolutePath(argv[2]);

    // Fail early if the DLL is missing - better than a silent remote failure.
    if (::GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"error: cannot find DLL '%ls'\n", dllPath.c_str());
        return 1;
    }

    DWORD pid = 0;
    if (IsNumeric(target)) {
        pid = static_cast<DWORD>(_wtoi(target.c_str()));
    } else {
        pid = FindProcessIdByName(target);
    }

    if (pid == 0) {
        std::fwprintf(stderr, L"error: could not find process '%ls'\n", target.c_str());
        return 1;
    }

    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    const HANDLE process = ::OpenProcess(access, FALSE, pid);
    if (process == nullptr) {
        return Fail(L"OpenProcess failed (is the game running as admin?)");
    }

    // Warn if we somehow targeted a 32-bit process; our x64 DLL will not load.
    BOOL targetIsWow64 = FALSE;
    if (::IsWow64Process(process, &targetIsWow64) && targetIsWow64) {
        std::fwprintf(stderr,
                      L"warning: target is a 32-bit (WOW64) process; an x64 DLL cannot be "
                      L"injected into it.\n");
    }

    const SIZE_T size = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = ::VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    if (remote == nullptr) {
        ::CloseHandle(process);
        return Fail(L"VirtualAllocEx failed");
    }

    if (!::WriteProcessMemory(process, remote, dllPath.c_str(), size, nullptr)) {
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return Fail(L"WriteProcessMemory failed");
    }

    // Resolve LoadLibraryW from our own kernel32. System DLLs share the same
    // base across processes in a session, so this address is valid remotely.
    const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    const auto loadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(::GetProcAddress(kernel32, "LoadLibraryW"));
    if (loadLibrary == nullptr) {
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return Fail(L"could not resolve LoadLibraryW");
    }

    const HANDLE thread =
        ::CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (thread == nullptr) {
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return Fail(L"CreateRemoteThread failed");
    }

    ::WaitForSingleObject(thread, 10000);

    DWORD exitCode = 0;
    ::GetExitCodeThread(thread, &exitCode);

    ::CloseHandle(thread);
    ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    ::CloseHandle(process);

    if (exitCode == 0) {
        std::fwprintf(stderr,
                      L"warning: LoadLibraryW returned NULL (DLL failed to initialise). "
                      L"Check bitness and unlocker.ini.\n");
        return 2;
    }

    std::wprintf(L"injected '%ls' into PID %lu\n", dllPath.c_str(), pid);
    return 0;
}
