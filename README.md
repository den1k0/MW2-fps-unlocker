# MW2 (2009) 64-bit FPS / FOV Unlocker

A rebuild of the classic "FPS unlocker / FOV changer" approach for the updated
**64-bit** build of Call of Duty: Modern Warfare 2 (2009).

## Short answer

**Yes, it is possible.** The reason every existing tool stopped working is not
that the technique is wrong, it is that it was written for a 32-bit process with
hard-coded addresses. Rebuilding it as a 64-bit tool that scans for *code
signatures* instead of using fixed offsets restores the functionality.

## Why the old 32-bit tools stopped working

| Cause | Explanation |
|-------|-------------|
| **WOW64 barrier** | A 32-bit process cannot inject a 32-bit DLL into, or read/write the memory of, a 64-bit process. Both the injector and the module must be x64. |
| **New code layout** | The game was re-compiled (likely a newer MSVC x64 toolchain). Every hard-coded address and offset is now meaningless. |
| **Different ABI** | x64 drops `stdcall`/`cdecl` and passes arguments in `RCX/RDX/R8/R9`. The old signature bytes do not exist verbatim. |
| **RIP-relative addressing** | x64 references globals with `mov reg, [rip+disp32]`, so a value that used to be a plain absolute address is now a displacement relative to the instruction. |
| **Pointer widening** | Pointers are 8 bytes, structures are re-padded, arrays of pointers changed stride. |

The one thing that *does* survive a re-compile is the **shape of the code**. That
is why this project is signature-driven.

## How this project solves it

* 100 % x64 DLL + x64 injector (no WOW64 problem).
* **No hard-coded offsets.** Features are described by IDA-style byte
  signatures (`48 8B 05 ?? ?? ?? ??`). At run time the DLL finds the code inside
  the game module, derives the final address, and patches it.
* Config-file driven, so you can update a signature without recompiling.
* Three patch primitives covering the usual cases:
  * `bytes` – overwrite instructions (e.g. NOP out a clamp or flip a branch),
  * `float` – overwrite a float constant (e.g. FOV in degrees),
  * `float_ptr` – resolve a RIP-relative global and write a float into it.
* Runtime hotkey toggle with original bytes restored on toggle.

## Repository layout

```
CMakeLists.txt          Build (DLL + injector, x64 enforced)
config/unlocker.ini     Feature/signature configuration (shipped next to the DLL)
src/log.*               Minimal file + debug-output logger
src/memory.*            Module range lookup, safe reads/writes, page iteration
src/scanner.*           AOB signature parser + scanner + RIP-relative resolver
src/patcher.*           Register patches, apply / restore / toggle
src/config.*            Tiny INI parser
src/features.*          Feature engine (fps / fov / float_ptr)
src/dllmain.cpp         Entry point, worker thread, hotkey, exported API
injector/main.cpp       x64 LoadLibrary injector
```

## Building

Requires Visual Studio 2019/2022 (Desktop C++ workload) and CMake ≥ 3.20.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Outputs in `build/Release/`:

```
mw2_unlocker.dll
injector.exe
unlocker.ini
```

Alternatively open the folder directly in Visual Studio ("Open a local folder")
and build the `mw2_unlocker` / `injector` targets with configuration `x64`.

## Finding the signatures for your build

You need one signature per feature. The workflow:

1. Launch the game and attach **x64dbg** (or Cheat Engine with the 64-bit
   target) to the game process.
2. For the **FPS cap**: the engine clamps the frame time / frame count against a
   constant. Search for the float constant the limiter compares against
   (MW2's classic multiplayer cap was 91.0, i.e. `1000 / 11`, driven by the
   network snapshot rate; single-player values differ). Find the compare /
   clamp, then walk to the branch that skips the frame.
   * For a branch-based clamp, the `bytes` patch is typically `90 90` (NOP the
     conditional jump) or flipping the jump's condition byte (`74` ↔ `75`).
3. For the **FOV**: find the render-FOV value (search the float, e.g. `65.0` or
   `80.0`, and change it), then locate the instruction that reads/writes it.
   * If it is written with an immediate `mov`, use `type=float` at the immediate.
   * If it lives in a global loaded as `movss xmm0, [rip+disp32]`, use
     `type=float_ptr`.
4. In x64dbg, select the instruction range around the site and copy it as an
   **array of bytes**; replace the parts that change between runs (absolute
   addresses inside `[]`, displacement bytes, immediates) with `??`.
5. Put the signature into `config/unlocker.ini`, set `enabled=1`, and choose a
   sensible `matchOffset` so the patch lands exactly on the byte(s) you want.

> Keep signatures **short and unique**. A 10–14 byte window that includes
> distinctive wildcards is usually enough. Verify the match is unique: if the
> tool finds the wrong spot, lengthen the signature.

## Usage — the easy way (single EXE)

Start MW2, then run:

```
build\Release\MW2Unlocker.exe
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
