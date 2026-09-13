## Usage — the easy way (single EXE)

Start MW2, then run:

```
YOUR_DIRECTORY_OF_CHOICE\MW2Unlocker.exe
```

That is the whole procedure. `MW2Unlocker.exe` is **self-contained** — the
unlocker DLL and the default config are embedded as resources, so there is one
file to double-click and no arguments to remember. It will:

1. extract the DLL and config to `%LOCALAPPDATA%\MW2Unlocker\`,
2. find the running game (`iw4mp.exe`, then `iw4sp.exe`),
3. inject, then print the config and log paths.

Press **F6** in game to toggle the patches on and off.

Worth knowing:

* It waits up to 30 seconds for the game, so you can launch it while MW2 is
  still loading.
* If the game runs as administrator, run the EXE as administrator too.
* `iw4x.exe` is still a **32-bit** client, so this x64 DLL cannot load into it —
  the launcher detects that and tells you instead of failing silently.
* If the DLL is already injected it says so. Re-injecting cannot re-run it, so
  either press **F6** twice (off, then on again) or restart the game.
* **`closeWithGame`** in `[general]` controls what the window does after
  injecting: `1` (default) keeps it open while the game runs and closes it with
  the game; `0` prints the summary and waits for a keypress instead. This
  setting is never applied when output is redirected, so scripts don't hang.
  The launcher adds the key to an existing config automatically if an older
  version of the file is missing it.
* Edit `%LOCALAPPDATA%\MW2Unlocker\unlocker.ini` to change the FOV, the max FPS
  value, or the toggle key. Your edits are kept — the embedded default is only
  written the first time. Delete the file to get the default back.

## Usage — the manual way

The DLL is configuration-driven and reads `unlocker.ini` from **its own
directory** (the DLL's path, not the game's) and writes `mw2_unlocker.log` next
to itself.

1. Edit `build/Release/unlocker.ini` for your build and features.
2. Inject with the bundled injector, by process name or PID:

```powershell
.\injector.exe iw4mp.exe "C:\path\to\mw2_unlocker.dll"
.\injector.exe 12345    "C:\path\to\mw2_unlocker.dll"
```

3. After `delayMs`, the patches are applied. Press the configured `toggleKey`
   (default **F6**) to toggle them on/off. Check `mw2_unlocker.log` for details.

### Exported API

If you prefer to drive the DLL yourself (script, another loader), it exports:

```
BOOL MW2U_Apply();       // scan + patch, returns success
void MW2U_Restore();     // restore original bytes
BOOL MW2U_Toggle();      // toggle, returns new state
BOOL MW2U_IsApplied();   // current state
```

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `cmake` not recognised | Install CMake and/or run from a **Developer Command Prompt for VS** so the MSVC environment is set up (`vcvars64.bat`). |
| `cmake` refuses to configure | You are on a 32-bit toolchain. Re-run with `-A x64`. |
| Injector exits with `OpenProcess failed` | Game not running, wrong exe name, or the game runs elevated while you do not. Run the injector as administrator. |
| `LoadLibraryW returned NULL` | DLL bitness mismatch (must be x64), or a dependency failed to load. |
| Log says *signature not found* | The signature does not match this build. Re-derive it (absolute addresses and displacements must be wildcards). |
| Log says *feature has no signature* | You left the placeholder signature / `enabled=0` off but never filled the signature in. |
| Patch applies but nothing changes | Wrong location (`matchOffset` off by a few bytes) or the value is re-written every frame by the game. Try patching the *source* (the clamp branch / the global) rather than a copy. |
| Game crashes on injection | Reduce `delayMs` reliance – often the process is scanned too early. Increase `delayMs`, and make sure you are not scanning `.data` regions containing recycled pointers. |

## Anti-cheat warning

MW2's multiplayer uses **VAC**. Writing into a VAC-protected process / altering
game code in multiplayer can result in a **ban**. This project is intended for
**single-player / private / offline** use (campaign and Spec Ops). Do not inject
into a VAC-secured multiplayer session. You are responsible for how you use it.

## Disclaimer

This is a reverse-engineering aid for a game you own, provided for educational
and personal single-player use. It ships with **no signatures** – you must
supply them for your build. No game code or assets are distributed.
