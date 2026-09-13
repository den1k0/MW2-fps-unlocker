// -----------------------------------------------------------------------------
// MW2 (2009) 64-bit unlocker - one-click launcher.
//
// Double-click this EXE with the game already running and it will:
//   1. extract the embedded unlocker DLL and default config into
//      %LOCALAPPDATA%\MW2Unlocker\   (so the EXE is a single self-contained file)
//   2. find the running game process (iw4mp.exe, then iw4sp.exe)
//   3. inject the DLL into it with the standard LoadLibraryW technique
//
// No command line arguments. Exit code 0 on success.
//
// The config is written only if it does not already exist, so your edits are
// preserved across runs. Delete %LOCALAPPDATA%\MW2Unlocker\unlocker.ini to get
// the default back.
// -----------------------------------------------------------------------------
#include <windows.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int kResourceDll = 101;
constexpr int kResourceIni = 102;

// Tried in order. iw4x.exe is intentionally last: it is still a 32-bit binary,
// so our 64-bit DLL cannot load into it, and we report that clearly.
const wchar_t* kGameCandidates[] = {L"iw4mp.exe", L"iw4sp.exe", L"iw4x.exe"};

constexpr wchar_t kDllName[] = L"mw2_unlocker.dll";
constexpr wchar_t kIniName[] = L"unlocker.ini";
constexpr wchar_t kLogName[] = L"mw2_unlocker.log";

void Line(const wchar_t* format, ...) {
    wchar_t buffer[1024] = {};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);
    std::wprintf(L"%ls\n", buffer);
}

std::wstring Trim(const std::wstring& value) {
    const wchar_t* whitespace = L" \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

// Read a text file as UTF-8. Bounded to 1 MB - these are config files.
bool ReadTextFile(const std::wstring& path, std::wstring& text) {
    const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (1 << 20)) {
        ::CloseHandle(file);
        return false;
    }

    std::string raw(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ::ReadFile(file, raw.data(), static_cast<DWORD>(raw.size()), &read, nullptr);
    ::CloseHandle(file);
    if (!ok || read == 0) {
        return false;
    }
    raw.resize(read);

    const int wideLength = ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(),
                                                 static_cast<int>(raw.size()), nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    text.assign(static_cast<size_t>(wideLength), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), text.data(),
                          wideLength);
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    const int narrowLength = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                                   static_cast<int>(text.size()), nullptr, 0,
                                                   nullptr, nullptr);
    if (narrowLength <= 0) {
        return false;
    }
    std::string narrow(static_cast<size_t>(narrowLength), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), narrow.data(),
                          narrowLength, nullptr, nullptr);

    const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, narrow.data(), static_cast<DWORD>(narrow.size()), &written,
                                nullptr);
    ::CloseHandle(file);
    return ok && written == narrow.size();
}

// Minimal INI read - enough for `[section]` + `key=value`. Returns `fallback`
// when the file, the section or the key is missing. Used by the launcher to
// pick up its own settings from the same unlocker.ini the DLL reads.
int ReadIniInt(const std::wstring& path, const wchar_t* section, const wchar_t* key,
               int fallback) {
    std::wstring text;
    if (!ReadTextFile(path, text)) {
        return fallback;
    }

    std::wstring currentSection;
    size_t position = 0;
    while (position <= text.size()) {
        size_t lineEnd = text.find(L'\n', position);
        if (lineEnd == std::wstring::npos) {
            lineEnd = text.size();
        }
        const std::wstring line = Trim(text.substr(position, lineEnd - position));
        position = lineEnd + 1;

        if (line.empty() || line[0] == L';' || line[0] == L'#') {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            currentSection = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }

        const std::wstring name = Trim(line.substr(0, equals));
        const std::wstring value = Trim(line.substr(equals + 1));
        if (_wcsicmp(currentSection.c_str(), section) == 0 &&
            _wcsicmp(name.c_str(), key) == 0) {
            if (value.empty()) {
                return fallback;
            }
            return static_cast<int>(::wcstol(value.c_str(), nullptr, 0));
        }
    }
    return fallback;
}

// Insert `key=value` directly after the `[section]` header when the key is not
// present yet. It is placed inside the existing section rather than appended as
// a second one, because the DLL's parser keys sections by name and a duplicate
// would clobber the first copy's settings.
bool EnsureIniKey(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                  int value) {
    std::wstring text;
    if (!ReadTextFile(path, text)) {
        return false;
    }

    const std::wstring keyEquals = std::wstring(key) + L"=";
    if (text.find(keyEquals) != std::wstring::npos) {
        return true; // already configured
    }

    const std::wstring header = std::wstring(L"[") + section + L"]";
    const size_t headerPosition = text.find(header);
    if (headerPosition == std::wstring::npos) {
        return false;
    }

    size_t insertAt = text.find(L'\n', headerPosition);
    if (insertAt == std::wstring::npos) {
        insertAt = text.size();
        text += L"\r\n";
    } else {
        ++insertAt; // keep the newline that terminates the header line
    }

    const std::wstring inserted = std::wstring(key) + L"=" + std::to_wstring(value) + L"\r\n";
    text.insert(insertAt, inserted);

    return WriteTextFile(path, text);
}

std::wstring LocalAppDataDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }
    return std::wstring(buffer, length);
}

std::wstring WorkDirectory() {
    return LocalAppDataDirectory() + L"\\MW2Unlocker";
}

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::wstring(L".");
    }
    const std::wstring full(path, length);
    const size_t slash = full.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring(L".") : full.substr(0, slash);
}

bool EnsureDirectory(const std::wstring& directory) {
    const DWORD attributes = ::GetFileAttributesW(directory.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return ::CreateDirectoryW(directory.c_str(), nullptr) != FALSE;
}

// Pull an embedded RCDATA blob out to a file. When `overwrite` is false and the
// destination already exists, the existing file is kept (used for the config so
// user edits survive).
bool ExtractResource(int id, const std::wstring& destination, bool overwrite) {
    if (!overwrite && ::GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    const HRSRC resource = ::FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (resource == nullptr) {
        Line(L"  ! embedded resource %d not found", id);
        return false;
    }

    const HGLOBAL loaded = ::LoadResource(nullptr, resource);
    if (loaded == nullptr) {
        Line(L"  ! could not load embedded resource %d", id);
        return false;
    }

    const void* data = ::LockResource(loaded);
    const DWORD size = ::SizeofResource(nullptr, resource);
    if (data == nullptr || size == 0) {
        Line(L"  ! embedded resource %d is empty", id);
        return false;
    }

    const HANDLE file = ::CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Line(L"  ! cannot write %ls (error %lu)", destination.c_str(), ::GetLastError());
        return false;
    }

    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, data, size, &written, nullptr);
    ::CloseHandle(file);

    if (!ok || written != size) {
        Line(L"  ! short write to %ls", destination.c_str());
        return false;
    }
    return true;
}

DWORD FindProcessId(const std::wstring& exeName) {
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (::Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }

    ::CloseHandle(snapshot);
    return pid;
}

bool Is32BitProcess(DWORD pid) {
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    BOOL wow64 = FALSE;
    ::IsWow64Process(process, &wow64);
    ::CloseHandle(process);
    return wow64 != FALSE;
}

bool IsModuleLoaded(DWORD pid, const wchar_t* moduleName) {
    const HANDLE snapshot =
        ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (::Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, moduleName) == 0) {
                found = true;
                break;
            }
        } while (::Module32NextW(snapshot, &entry));
    }

    ::CloseHandle(snapshot);
    return found;
}

bool InjectDll(DWORD pid, const std::wstring& dllPath) {
    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;

    const HANDLE process = ::OpenProcess(access, FALSE, pid);
    if (process == nullptr) {
        Line(L"  ! OpenProcess failed (error %lu).", ::GetLastError());
        Line(L"    If the game runs as administrator, run this EXE as administrator too.");
        return false;
    }

    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = ::VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    if (remote == nullptr) {
        Line(L"  ! VirtualAllocEx failed (error %lu)", ::GetLastError());
        ::CloseHandle(process);
        return false;
    }

    if (!::WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
        Line(L"  ! WriteProcessMemory failed (error %lu)", ::GetLastError());
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return false;
    }

    const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    const auto loadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(::GetProcAddress(kernel32, "LoadLibraryW"));
    if (loadLibrary == nullptr) {
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return false;
    }

    const HANDLE thread =
        ::CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (thread == nullptr) {
        Line(L"  ! CreateRemoteThread failed (error %lu)", ::GetLastError());
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return false;
    }

    ::WaitForSingleObject(thread, 15000);

    DWORD exitCode = 0;
    ::GetExitCodeThread(thread, &exitCode);

    ::CloseHandle(thread);
    ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    ::CloseHandle(process);

    if (exitCode == 0) {
        Line(L"  ! LoadLibraryW returned NULL - the DLL failed to initialise.");
        return false;
    }
    return true;
}

} // namespace

// Defined below; declared here because Run() calls them.
bool IsInteractiveConsole();
void WatchUntilGameExits(DWORD pid);
void Finish(DWORD pid, bool closeWithGame);

int Run() {
    std::wprintf(L"MW2 (2009) 64-bit unlocker\n");
    std::wprintf(L"--------------------------\n");

    const std::wstring work = WorkDirectory();
    if (!EnsureDirectory(work)) {
        Line(L"! Cannot create %ls", work.c_str());
        return 1;
    }

    // ---- extract our payload -------------------------------------------------
    const std::wstring dllPath = work + L"\\" + kDllName;
    const std::wstring iniPath = work + L"\\" + kIniName;
    const std::wstring logPath = work + L"\\" + kLogName;

    Line(L"Work folder: %ls", work.c_str());

    // The DLL cannot be overwritten while the game has it loaded, so fall back
    // to whatever copy is already on disk.
    if (!ExtractResource(kResourceDll, dllPath, true)) {
        if (::GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Line(L"! No usable DLL - aborting.");
            return 1;
        }
        Line(L"  (using the existing copy already on disk)");
    } else {
        Line(L"  DLL written.");
    }

    // A config sitting next to MW2Unlocker.exe takes priority over both the
    // embedded default and any previously stored copy. There is otherwise a
    // real trap here: this tool keeps its working config under %LOCALAPPDATA%,
    // but the unlocker.ini a user sees next to the EXE is where they will
    // naturally edit - and edits there would silently do nothing.
    const std::wstring localIni = ExeDirectory() + L"\\" + kIniName;

    if (::GetFileAttributesW(localIni.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (::CopyFileW(localIni.c_str(), iniPath.c_str(), FALSE)) {
            Line(L"  Config: using the unlocker.ini next to the EXE.");
        } else {
            Line(L"  ! Could not read the unlocker.ini next to the EXE - using the stored copy.");
        }
    } else if (!ExtractResource(kResourceIni, iniPath, false)) {
        if (::GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Line(L"! No usable config - aborting.");
            return 1;
        }
    } else {
        Line(L"  Config ready (existing edits kept).");
    }

    // ---- launcher behaviour, read from the same ini the DLL uses ------------
    // Make sure the setting exists before reading it: the extracted config is
    // only written once, so a file left by an older build would not have it yet.
    EnsureIniKey(iniPath, L"general", L"closeWithGame", 1);

    // closeWithGame=1 (default): stay open while the game runs, exit with it.
    // closeWithGame=0:           print the summary and wait for a keypress.
    const int closeWithGame = ReadIniInt(iniPath, L"general", L"closeWithGame", 1);

    // ---- locate the game -----------------------------------------------------
    const wchar_t* foundName = nullptr;
    DWORD pid = 0;

    for (int attempt = 0; attempt < 60 && pid == 0; ++attempt) {
        for (const wchar_t* candidate : kGameCandidates) {
            const DWORD candidatePid = FindProcessId(candidate);
            if (candidatePid != 0) {
                foundName = candidate;
                pid = candidatePid;
                break;
            }
        }
        if (pid == 0) {
            if (attempt == 0) {
                std::wprintf(L"Waiting for the game (start it now)...\n");
            }
            std::wprintf(L".");
            ::Sleep(500);
        }
    }

    if (pid == 0) {
        std::wprintf(L"\n");
        Line(L"! The game is not running. Start MW2 first, then run this again.");
        Line(L"  Looked for: iw4mp.exe, iw4sp.exe, iw4x.exe");
        return 1;
    }
    std::wprintf(L"\n");

    Line(L"Game: %ls (PID %lu)", foundName, pid);

    if (Is32BitProcess(pid)) {
        Line(L"! That process is 32-bit, and this unlocker is 64-bit.");
        Line(L"  iw4x.exe is a 32-bit client and needs a separate 32-bit build.");
        Line(L"  Use iw4mp.exe (multiplayer) or iw4sp.exe (single player).");
        return 1;
    }

    if (IsModuleLoaded(pid, kDllName)) {
        Line(L"The unlocker is ALREADY injected into this session.");
        Line(L"  Press F6 in game to toggle it off, then F6 again to re-apply.");
        Line(L"  (Re-injecting cannot re-run it - restart the game for a clean apply.)");
        Finish(pid, closeWithGame != 0);
        return 0;
    }

    // ---- inject --------------------------------------------------------------
    if (!InjectDll(pid, dllPath)) {
        Line(L"! Injection failed.");
        return 1;
    }

    Line(L"Injected successfully.");
    Line(L"");
    Line(L"  Wait for the configured delayMs, then press F6 in game to toggle.");
    Line(L"  Config: %ls", iniPath.c_str());
    Line(L"  Log:    %ls", logPath.c_str());

    Finish(pid, closeWithGame != 0);

    return 0;
}

// True when stdout really is a console, i.e. the user double-clicked the EXE
// rather than redirecting or piping its output.
bool IsInteractiveConsole() {
    const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return out != nullptr && out != INVALID_HANDLE_VALUE &&
           ::GetConsoleMode(out, &mode) != FALSE;
}

// Set once we have already held the window open by watching the game, so we do
// not afterwards also sit there waiting for a keypress.
bool g_waitedForGame = false;

// Stay resident while the game runs and exit with it, so a double-click leaves
// no stray window behind.
void WatchUntilGameExits(DWORD pid) {
    const HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        return;
    }

    Line(L"");
    Line(L"Watching the game - this window closes automatically when MW2 does.");
    ::WaitForSingleObject(process, INFINITE);
    ::CloseHandle(process);

    Line(L"Game closed - exiting.");
    g_waitedForGame = true;
}

// Applies the `closeWithGame` setting on every "we are done" path. Only ever
// acts on a real console, so redirected or scripted runs never block.
void Finish(DWORD pid, bool closeWithGame) {
    if (!IsInteractiveConsole()) {
        return;
    }
    if (closeWithGame) {
        WatchUntilGameExits(pid);
    }
    // closeWithGame=0 falls through to wmain's PauseIfInteractive(), which holds
    // the window open until a key is pressed so the summary can be read.
}

// Used only for the error paths now: if we never watched a game, the window
// would vanish before the reason for the failure could be read. Redirected or
// piped output is left alone so scripts and CI never block.
void PauseIfInteractive() {
    if (g_waitedForGame || !IsInteractiveConsole()) {
        return;
    }

    std::wprintf(L"\nPress any key to close...");

    const HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD inputMode = 0;
    if (in != nullptr && in != INVALID_HANDLE_VALUE &&
        ::GetConsoleMode(in, &inputMode) != FALSE) {
        INPUT_RECORD record{};
        DWORD read = 0;
        for (;;) {
            if (!::ReadConsoleInputW(in, &record, 1, &read)) {
                break;
            }
            if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
                break;
            }
        }
    }
    std::wprintf(L"\n");
}

int wmain() {
    const int result = Run();
    PauseIfInteractive();
    return result;
}
