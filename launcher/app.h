#pragma once

#include <windows.h>

#include <string>

#include "ipc.h"

// -----------------------------------------------------------------------------
// Launcher plumbing shared by main.cpp (paths, payload, process, injection) and
// gui.cpp (the window).
//
// The split exists so the window does not have to know how a process is found or
// how a DLL is injected, and so the injection path can be tested from the
// command line without a window in the way.
// -----------------------------------------------------------------------------
namespace app {

// Where the working copy of the DLL, the config and the log live.
std::wstring WorkDirectory();
std::wstring WorkDllPath();
std::wstring WorkIniPath();
std::wstring WorkLogPath();
std::wstring ExeDirectory();

// The config sitting next to MW2Unlocker.exe, which takes priority over the
// stored copy when present.
std::wstring NextToExeIniPath();

// Writes to stdout only when the caller supplied one - a shell, or a redirect.
// Started from Explorer there is no console, and the window is the interface.
void Log(const wchar_t* format, ...);

// Text and INI handling. SetIniValue rewrites the one line that sets a key and
// leaves the rest of the file, comments included, alone; a commented-out
// ";key=value" counts as the key being present, which is how the shipped config
// documents `max=` and how editing it in the window uncomments it.
bool ReadTextFile(const std::wstring& path, std::wstring& text);
bool WriteTextFile(const std::wstring& path, const std::wstring& text);
bool SetIniValue(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                 const std::wstring& value);
int IniInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback);
float IniFloat(const std::wstring& path, const wchar_t* section, const wchar_t* key,
               float fallback);

// The game's own settings file, in a "players" folder next to its executable.
//
// Some settings are never read from the dvar at run time. The mouse sensitivity
// is one: the game copies it into the player profile when it starts and the
// aiming code reads the profile from then on, so writing the dvar changes a
// number that nothing consults. The config file is the path that works - it just
// needs the next launch.
std::wstring GameConfigPath(const std::wstring& gameDirectory, const std::wstring& gameName);

// Set one "seta key value" line in a game config file. That file is a flat list
// of settings with no sections, and the line is rewritten in place or appended.
bool SetGameConfigValue(const std::wstring& path, const std::wstring& key,
                        const std::wstring& value);

// Full path of a running process's executable, used to find the game folder.
bool ProcessPath(DWORD pid, std::wstring& path);

// Extract the embedded DLL and default config into the work folder. The config
// is only written when it is absent, so user edits survive.
bool PreparePayload(std::wstring& error);

struct Game {
    std::wstring name;
    DWORD pid = 0;
};

// First running game from the candidate list. iw4x.exe is included so that the
// 32-bit case can be reported properly rather than looking like "no game".
bool FindGame(Game& game);
bool Is32Bit(DWORD pid);
bool IsUnlockerLoaded(DWORD pid);
bool Inject(DWORD pid, const std::wstring& dllPath, std::wstring& error);

// Send values to the injected DLL. Returns false when there is no live channel
// yet, which is not necessarily a failure: the values have already been saved to
// the config and will be applied when the DLL starts.
bool PushLive(const ipc::Values& values);
bool LiveChannelPresent();

} // namespace app
