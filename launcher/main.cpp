// -----------------------------------------------------------------------------
// MW2 (2009) 64-bit unlocker - launcher plumbing.
//
// Owns everything that happens around the window: pulling the embedded DLL and
// default config out of this EXE, finding the game process, injecting, and
// pushing values down the live channel. The window itself is gui.cpp.
//
// This is a GUI-subsystem binary, so double-clicking gives the window and
// nothing else. stdio still works when a caller provides it, so running it from
// a shell - or redirecting its output - prints the same diagnostics the earlier
// console build printed.
// -----------------------------------------------------------------------------
#include <windows.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"
#include "gui.h"
#include "ipc.h"

namespace app {
namespace {

constexpr int kResourceDll = 101;
constexpr int kResourceIni = 102;

constexpr wchar_t kDllName[] = L"mw2_unlocker.dll";
constexpr wchar_t kIniName[] = L"unlocker.ini";
constexpr wchar_t kLogName[] = L"mw2_unlocker.log";

// Tried in order. iw4x.exe is deliberately last: it is still a 32-bit client, so
// the 64-bit unlocker cannot load into it, and finding it lets us say that
// instead of reporting that no game is running.
const wchar_t* kGameCandidates[] = {L"iw4mp.exe", L"iw4sp.exe", L"iw4x.exe"};

std::wstring Trim(const std::wstring& value) {
    const wchar_t* whitespace = L" \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

// A trailing "; note" on a `key=value` line, or empty. Kept when a line is
// rewritten so an inline note after a value is not silently dropped.
std::wstring TrailingComment(const std::wstring& line) {
    const size_t equals = line.find(L'=');
    if (equals == std::wstring::npos) {
        return std::wstring();
    }
    for (size_t i = equals + 1; i < line.size(); ++i) {
        if ((line[i] == L';' || line[i] == L'#') && (line[i - 1] == L' ' || line[i - 1] == L'\t')) {
            return line.substr(i);
        }
    }
    return std::wstring();
}

// Split text into lines without their separators, and put it back together with
// the separator the file already used. Both file editors here need the same two
// steps, and both must leave everything they do not touch byte for byte alone.
void SplitLines(const std::wstring& text, std::vector<std::wstring>& lines) {
    size_t position = 0;
    while (position < text.size()) {
        size_t lineEnd = text.find(L'\n', position);
        if (lineEnd == std::wstring::npos) {
            lineEnd = text.size();
        }
        std::wstring line = text.substr(position, lineEnd - position);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        lines.push_back(line);
        position = lineEnd + 1;
    }
}

std::wstring JoinLines(const std::vector<std::wstring>& lines, const std::wstring& eol) {
    std::wstring output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        if (i + 1 < lines.size()) {
            output += eol;
        }
    }
    return output;
}

bool StartsWithWord(const std::wstring& text, const std::wstring& word) {
    if (text.size() < word.size()) {
        return false;
    }
    if (_wcsnicmp(text.c_str(), word.c_str(), word.size()) != 0) {
        return false;
    }
    return text.size() == word.size() || text[word.size()] == L' ' || text[word.size()] == L'\t';
}

std::wstring LocalAppDataDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }
    return std::wstring(buffer, length);
}

bool EnsureDirectory(const std::wstring& directory) {
    const DWORD attributes = ::GetFileAttributesW(directory.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return ::CreateDirectoryW(directory.c_str(), nullptr) != FALSE;
}

// Pull an embedded RCDATA blob out to a file. With `overwrite` false an existing
// destination is kept, which is what preserves user edits to the config.
bool ExtractResource(int id, const std::wstring& destination, bool overwrite) {
    if (!overwrite && ::GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    const HRSRC resource = ::FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (resource == nullptr) {
        Log(L"  ! embedded resource %d not found", id);
        return false;
    }

    const HGLOBAL loaded = ::LoadResource(nullptr, resource);
    if (loaded == nullptr) {
        Log(L"  ! could not load embedded resource %d", id);
        return false;
    }

    const void* data = ::LockResource(loaded);
    const DWORD size = ::SizeofResource(nullptr, resource);
    if (data == nullptr || size == 0) {
        Log(L"  ! embedded resource %d is empty", id);
        return false;
    }

    const HANDLE file = ::CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Log(L"  ! cannot write %ls (error %lu)", destination.c_str(), ::GetLastError());
        return false;
    }

    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, data, size, &written, nullptr);
    ::CloseHandle(file);

    if (!ok || written != size) {
        Log(L"  ! short write to %ls", destination.c_str());
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

// Look up one `key=value` inside `[section]`. Used for the few settings the
// launcher itself reads, out of the same file the DLL uses.
bool IniLookup(const std::wstring& path, const wchar_t* section, const wchar_t* key,
               std::wstring& value) {
    std::wstring text;
    if (!ReadTextFile(path, text)) {
        return false;
    }

    std::wstring currentSection;
    size_t position = 0;
    while (position < text.size()) {
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
        if (_wcsicmp(currentSection.c_str(), section) == 0 &&
            _wcsicmp(name.c_str(), key) == 0) {
            value = Trim(line.substr(equals + 1));
            return true;
        }
    }
    return false;
}

} // namespace

void Log(const wchar_t* format, ...) {
    // Started from Explorer there is no stdio at all and the window is the
    // interface; when a shell provided handles, a copy of the log is useful.
    const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        return;
    }

    wchar_t buffer[1024] = {};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);
    std::wprintf(L"%ls\n", buffer);
}

std::wstring WorkDirectory() {
    return LocalAppDataDirectory() + L"\\MW2Unlocker";
}

std::wstring WorkDllPath() {
    return WorkDirectory() + L"\\" + kDllName;
}

std::wstring WorkIniPath() {
    return WorkDirectory() + L"\\" + kIniName;
}

std::wstring WorkLogPath() {
    return WorkDirectory() + L"\\" + kLogName;
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

std::wstring NextToExeIniPath() {
    return ExeDirectory() + L"\\" + kIniName;
}

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

bool SetIniValue(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                 const std::wstring& value) {
    std::wstring text;
    if (!ReadTextFile(path, text)) {
        return false;
    }

    // Whatever the file already uses, so the whole thing is not rewritten as LF.
    const std::wstring eol = (text.find(L"\r\n") != std::wstring::npos) ? L"\r\n" : L"\n";

    std::vector<std::wstring> lines;
    SplitLines(text, lines);

    const std::wstring header = std::wstring(L"[") + section + L"]";
    const std::wstring keyEquals = std::wstring(key) + L"=";
    const std::wstring replacement = std::wstring(key) + L"=" + value;

    bool inSection = false;
    bool handled = false;

    for (size_t i = 0; i < lines.size() && !handled; ++i) {
        const std::wstring trimmed = Trim(lines[i]);

        if (!trimmed.empty() && trimmed.front() == L'[' && trimmed.back() == L']') {
            if (inSection) {
                // Left the target section without finding the key: put it at the
                // end of that section rather than in whatever follows.
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i), replacement);
                handled = true;
                break;
            }
            inSection = _wcsicmp(Trim(trimmed.substr(1, trimmed.size() - 2)).c_str(), section) == 0;
            continue;
        }

        if (!inSection) {
            continue;
        }

        // A commented-out setting counts as present. That is how the shipped
        // config documents `max=`, and toggling the clamp here should uncomment
        // that line rather than add a second, conflicting one.
        std::wstring body = trimmed;
        if (!body.empty() && (body[0] == L';' || body[0] == L'#')) {
            body = Trim(body.substr(1));
        }

        if (body.compare(0, keyEquals.size(), keyEquals) == 0) {
            const std::wstring comment = TrailingComment(lines[i]);
            lines[i] = replacement + (comment.empty() ? L"" : L" " + comment);
            handled = true;
        }
    }

    if (!handled) {
        if (!inSection) {
            // The section does not exist at all.
            if (!lines.empty() && !lines.back().empty()) {
                lines.push_back(std::wstring());
            }
            lines.push_back(header);
        }
        lines.push_back(replacement);
    }

    return WriteTextFile(path, JoinLines(lines, eol));
}

bool SetGameConfigValue(const std::wstring& path, const std::wstring& key,
                        const std::wstring& value) {
    std::wstring text;
    if (!ReadTextFile(path, text)) {
        return false;
    }

    const std::wstring eol = (text.find(L"\r\n") != std::wstring::npos) ? L"\r\n" : L"\n";

    std::vector<std::wstring> lines;
    SplitLines(text, lines);

    // Quoted, because that is how the game writes its own: `seta sensitivity "5"`.
    // Matching the format exactly matters here - the file is rewritten by the game
    // from its own memory, and a line that does not look like its own is more
    // likely to be dropped by its parser.
    //
    // A hand-edited file can say `set name value` or just `name value`, so all
    // three forms are recognised when looking for the line to replace.
    const std::wstring replacement = L"seta " + key + L" \"" + value + L"\"";

    bool handled = false;
    for (size_t i = 0; i < lines.size() && !handled; ++i) {
        std::wstring body = Trim(lines[i]);
        if (body.empty() || body[0] == L';' || body[0] == L'#') {
            continue;
        }

        if (body.compare(0, 5, L"seta ") == 0) {
            body = Trim(body.substr(5));
        } else if (body.compare(0, 4, L"set ") == 0) {
            body = Trim(body.substr(4));
        }

        if (StartsWithWord(body, key)) {
            lines[i] = replacement;
            handled = true;
        }
    }

    if (!handled) {
        lines.push_back(replacement);
    }

    return WriteTextFile(path, JoinLines(lines, eol));
}

std::wstring GameConfigPath(const std::wstring& gameDirectory, const std::wstring& gameName) {
    // The two clients keep separate files, and both are read at startup.
    const bool multiplayer = _wcsicmp(gameName.c_str(), L"iw4mp.exe") == 0;
    return gameDirectory + (multiplayer ? L"\\players\\config_mp.cfg" : L"\\players\\config.cfg");
}

bool ProcessPath(DWORD pid, std::wstring& path) {
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }

    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = _countof(buffer);
    const BOOL ok = ::QueryFullProcessImageNameW(process, 0, buffer, &size);
    ::CloseHandle(process);
    if (!ok) {
        return false;
    }

    path.assign(buffer, size);
    return true;
}

int IniInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    std::wstring value;
    if (!IniLookup(path, section, key, value) || value.empty()) {
        return fallback;
    }
    return static_cast<int>(::wcstol(value.c_str(), nullptr, 0));
}

float IniFloat(const std::wstring& path, const wchar_t* section, const wchar_t* key,
               float fallback) {
    std::wstring value;
    if (!IniLookup(path, section, key, value) || value.empty()) {
        return fallback;
    }
    return static_cast<float>(::wcstod(value.c_str(), nullptr));
}

bool PreparePayload(std::wstring& error) {
    const std::wstring work = WorkDirectory();
    if (!EnsureDirectory(work)) {
        error = L"Cannot create the working folder:\n" + work;
        return false;
    }
    Log(L"Work folder: %ls", work.c_str());

    // The DLL cannot be overwritten while the game has it loaded, so fall back to
    // whatever copy is already on disk rather than refusing to start.
    const std::wstring dllPath = WorkDllPath();
    if (!ExtractResource(kResourceDll, dllPath, true)) {
        if (::GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            error = L"The embedded unlocker DLL is missing and no copy exists on disk.";
            return false;
        }
        Log(L"  (using the copy of the DLL already on disk)");
    } else {
        Log(L"  DLL written.");
    }

    // A config next to MW2Unlocker.exe takes priority over the embedded default
    // and over any previously stored copy. Without this there is a real trap:
    // the working config lives under %LOCALAPPDATA%, while the unlocker.ini a
    // user sees next to the EXE is the one they will naturally edit - and edits
    // there would silently do nothing.
    const std::wstring iniPath = WorkIniPath();
    const std::wstring nextToExe = NextToExeIniPath();

    if (::GetFileAttributesW(nextToExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (::CopyFileW(nextToExe.c_str(), iniPath.c_str(), FALSE)) {
            Log(L"  Config: using the unlocker.ini next to the EXE.");
        } else {
            Log(L"  ! Could not read the unlocker.ini next to the EXE; using the stored copy.");
        }
    } else if (!ExtractResource(kResourceIni, iniPath, false)) {
        if (::GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            error = L"There is no configuration file and no embedded default.";
            return false;
        }
    } else {
        Log(L"  Config ready (existing edits kept).");
    }
    return true;
}

bool FindGame(Game& game) {
    game = Game();
    for (const wchar_t* candidate : kGameCandidates) {
        const DWORD pid = FindProcessId(candidate);
        if (pid != 0) {
            game.name = candidate;
            game.pid = pid;
            return true;
        }
    }
    return false;
}

bool Is32Bit(DWORD pid) {
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    BOOL wow64 = FALSE;
    ::IsWow64Process(process, &wow64);
    ::CloseHandle(process);
    return wow64 != FALSE;
}

bool IsUnlockerLoaded(DWORD pid) {
    return IsModuleLoaded(pid, kDllName);
}

bool Inject(DWORD pid, const std::wstring& dllPath, std::wstring& error) {
    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;

    const HANDLE process = ::OpenProcess(access, FALSE, pid);
    if (process == nullptr) {
        error = L"OpenProcess failed (error " + std::to_wstring(::GetLastError()) + L")";
        return false;
    }

    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = ::VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    if (remote == nullptr) {
        error = L"VirtualAllocEx failed (error " + std::to_wstring(::GetLastError()) + L")";
        ::CloseHandle(process);
        return false;
    }

    if (!::WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
        error = L"WriteProcessMemory failed (error " + std::to_wstring(::GetLastError()) + L")";
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return false;
    }

    const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    const auto loadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(::GetProcAddress(kernel32, "LoadLibraryW"));
    if (loadLibrary == nullptr) {
        error = L"LoadLibraryW could not be resolved";
        ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        ::CloseHandle(process);
        return false;
    }

    const HANDLE thread =
        ::CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (thread == nullptr) {
        error = L"CreateRemoteThread failed (error " + std::to_wstring(::GetLastError()) + L")";
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
        error = L"LoadLibraryW returned NULL - the DLL failed to initialise";
        return false;
    }
    return true;
}

bool LiveChannelPresent() {
    return ::FindWindowW(ipc::kWindowClass, nullptr) != nullptr;
}

bool PushLive(const ipc::Values& values) {
    const HWND window = ::FindWindowW(ipc::kWindowClass, nullptr);
    if (window == nullptr) {
        return false;
    }

    COPYDATASTRUCT data{};
    data.dwData = static_cast<ULONG_PTR>(ipc::kCopyDataMagic);
    data.cbData = sizeof(values);
    // The kernel copies this, and the payload it points at, into the game during
    // the call. That is the whole reason WM_COPYDATA is used rather than a
    // message of our own: see the note in src/ipc.h.
    data.lpData = const_cast<ipc::Values*>(&values);

    // SendMessageTimeout rather than SendMessage: if the game is busy, hung or
    // being debugged, the window must not freeze along with it.
    DWORD_PTR result = 0;
    const LRESULT sent = ::SendMessageTimeoutW(window, WM_COPYDATA, 0,
                                               reinterpret_cast<LPARAM>(&data),
                                               SMTO_ABORTIFHUNG | SMTO_NORMAL, 2000, &result);
    if (sent == 0) {
        Log(L"  ! live update did not reach the game (error %lu)", ::GetLastError());
        return false;
    }
    Log(L"  live update delivered (result %llu)", static_cast<unsigned long long>(result));
    return result != 0;
}

} // namespace app

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE /*previous*/, LPWSTR /*commandLine*/,
                      int /*showCommand*/) {
    std::wstring error;
    if (!app::PreparePayload(error)) {
        // Nothing is on screen yet, so a message box is the only way to be seen.
        ::MessageBoxW(nullptr, error.c_str(), L"MW2 Unlocker", MB_ICONERROR | MB_OK);
        return 1;
    }
    return gui::Run(instance);
}
