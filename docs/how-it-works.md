# MW2 (2009) 64-bit FPS / FOV Unlocker

A rebuild of the classic "FPS unlocker / FOV changer" approach for the updated
**64-bit** build of Call of Duty: Modern Warfare 2 (2009).

## Short answer

**Yes, it is possible**, and this repository is a working implementation. The
reason every existing tool stopped working is not that the technique is wrong —
it is that those tools were written for a 32-bit process with hard-coded
addresses. Rebuilt as a 64-bit tool that finds the game's *own settings by name*
at run time, the functionality comes back.

## What it does

| Feature | What it changes | Config section |
|---------|-----------------|----------------|
| **Frame rate cap** | `com_maxfps` — an **integer** that defaults to `85`. `0` means uncapped. | `[fps]` |
| **Field of view** | `cg_fov` — plus the `80.0` clamp sitting at `dvar+0x44`, if you want a very wide FOV. | `[fov]` |
| **In-game FPS counter** | `cg_drawFPS` — the engine's own frame counter, so you are not dependent on an external overlay. **Single player only**: the multiplayer client registers this cvar and never reads it. | `[drawfps]` |

You only ever type a value:

```ini
[fps]
value=250

[fov]
value=90

[drawfps]
value=1
```

## Why the old 32-bit tools stopped working

| Cause | Explanation |
|-------|-------------|
| **WOW64 barrier** | A 32-bit process cannot inject a 32-bit DLL into, or read/write the memory of, a 64-bit process. Both the injector and the module must be x64. |
| **New code layout** | The game was re-compiled (likely a newer MSVC x64 toolchain). Every hard-coded address and offset is now meaningless. |
| **Different ABI** | x64 drops `stdcall`/`cdecl` and passes arguments in `RCX/RDX/R8/R9`. The old signature bytes do not exist verbatim. |
| **RIP-relative addressing** | x64 references globals with `mov reg, [rip+disp32]`, so a value that used to be a plain absolute address is now a displacement relative to the instruction. |
| **Pointer widening** | Pointers are 8 bytes, structures are re-padded, arrays of pointers changed stride. |

Two facts about *this* build are worth knowing, because they invalidate the
obvious approach:

* The multiplayer cap is **not** a float. It is `com_maxfps`, an integer
  defaulting to `85`. Searching memory for `91.0` or `91` never finds it, which
  is why the first attempts at this came up empty.
* The in-game counter is spelled **`cg_drawFPS`** — capital `FPS` — even though
  every guide writes `cg_drawfps`. A byte-exact search for the documented
  spelling finds nothing.

Both findings are written up with the evidence in
[`docs/finding-signatures.md`](docs/finding-signatures.md).

## How this project solves it

* 100 % x64 DLL + x64 injector (no WOW64 problem).
* **No hard-coded addresses.** A cvar is found by scanning the module for its
  null-terminated *name*, then scanning for the pointer to that name — which is
  the first field of the engine's `dvar_t`. That indirection is what lets one
  config work in both `iw4mp.exe` and `iw4sp.exe`, and survive every relaunch.
* Name lookups are **case-insensitive**, so `cg_drawFPS` and `cg_drawfps` both
  resolve.
* Config-file driven, so a value can be changed without recompiling.
* A watchdog re-applies values the engine resets (the engine re-initialises
  `cg_fov` on a respawn or level load, for example).
* Runtime hotkey toggle, restoring the original bytes on toggle-off.
* Five patch primitives, covering everything from a flagship preset to a raw
  byte patch:

| `type=` | Use |
|---------|-----|
| *(none — a preset section name)* | `[fps]`, `[fov]`, `[drawfps]` — the friendly path |
| `dvar_int` | write an integer into a cvar's value slots |
| `dvar_float` | write a float into a cvar's value slots |
| `bytes` | overwrite instructions (NOP out a clamp, flip a branch) |
| `float` | overwrite a float constant at a signature match |
| `float_ptr` | resolve a RIP-relative global and write a float into it |

## Configuration reference

`unlocker.ini` lives next to the EXE (see *Usage*). Sections:

| Section | Keys | Notes |
|---------|------|-------|
| `[general]` | `delayMs`, `toggleKey`, `closeWithGame`, `keepApplied`, `keepAliveMs`, `gameExe` | `toggleKey=0x75` is F6. `keepAliveMs` is how often the watchdog re-checks; keep it small, the check is a few 4-byte reads. `0` disables it. |
| `[fps]` | `enabled`, `value` | `com_maxfps`. `0` = uncapped, `250` = sensible ceiling, `1000` = effectively uncapped. |
| `[fov]` | `enabled`, `value`, `max` | `cg_fov` in degrees. Uncomment `max=` to raise the engine's clamp as well. |
| `[drawfps]` | `enabled`, `value` | `cg_drawFPS`. `0` = off, `1` = on. |

Any section can be replaced by a fully manual one (`type=`, `cvar=`,
`valueOffsets=`, `signature=`, `patch=`). The tail of `unlocker.ini` documents
that form, and every number in it is derived in
[`docs/finding-signatures.md`](docs/finding-signatures.md).

## The in-game FPS counter, and why it may disagree with Steam

`[drawfps]` is not just cosmetic. An external overlay (Steam, RTSS) counts the
frames that are *presented* to it; the engine's own counter reports the rate the
engine *itself* computes, which is the number the game's movement and
physics code sees. They are not always the same figure, so if you care about
exact values — and in MW2 the classic movement tricks are tied to specific frame
rates — the engine's own counter is the one to read.

**It exists in single player only.** `iw4sp.exe` reads `cg_drawFPS` from six
places; `iw4mp.exe` registers the cvar and never reads it, so in multiplayer the
value is written and silently ignored and no counter can be switched on. That is
measured against both binaries rather than inferred — see
[`finding-signatures.md`](finding-signatures.md) for the method and the table.
The only counter multiplayer still has is `sv_network_fps` (`[netfps]`), which
reports the network rate rather than the frame rate.

There is a second trap: the frame limiter does not hold an arbitrary rate
exactly. It converts the request into a whole number of milliseconds and then
overshoots slightly, so `250` can present as something around `212`, and `167`
lands near `190`. The values that come out clean are the ones that divide 1000
evenly. If you need an exact rate, set `value=0` (uncapped) and limit the frame
rate externally with RTSS or the NVIDIA driver's *Max Frame Rate*.

## Repository layout

```
CMakeLists.txt          Build (DLL + injector + one-click launcher, x64 enforced)
config/unlocker.ini     Feature configuration (embedded into the launcher, copied next to the DLL)
src/log.*               Minimal file + debug-output logger
src/memory.*            Module range lookup, safe reads/writes, page iteration
src/scanner.*           AOB signature parser + scanner + case-insensitive name search
src/patcher.*           Register patches, apply / restore / toggle / re-apply
src/config.*            Tiny INI parser
src/features.*          Feature engine (presets, dvar lookup, float_ptr, bytes)
src/dllmain.cpp         Entry point, worker thread, hotkey, exported API
injector/main.cpp       x64 LoadLibrary injector
launcher/main.cpp       Self-contained launcher: extracts the embedded DLL + config, injects
docs/finding-signatures.md  The full reverse-engineering write-up
dist/                   Built, ready-to-run output
tools/                  Helper scripts used during the investigation
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
MW2Unlocker.exe
unlocker.ini
```

Close the game before rebuilding: a loaded DLL cannot be relinked, and the
linker fails with `LNK1104`.

## Usage — the easy way (single EXE)

Start MW2, then run:

```
dist\MW2Unlocker.exe
```

That is the whole procedure. `MW2Unlocker.exe` is **self-contained** — the
unlocker DLL and the default config are embedded as resources, so there is one
file to double-click and no arguments to remember. It will:

1. extract the DLL and config to `%LOCALAPPDATA%\MW2Unlocker\`,
2. find the running game (`iw4mp.exe`, then `iw4sp.exe`),
3. inject, then print the config and log paths.

Press **F6** in game to toggle every patch on and off at once.

Worth knowing:

* An `unlocker.ini` sitting **next to the EXE** wins over the stored copy and is
  copied into the work folder on every run. Edit that one. This matters because
  the working config lives under `%LOCALAPPDATA%`, and edits to a forgotten copy
  elsewhere would silently do nothing.
* It waits up to 30 seconds for the game, so you can launch it while MW2 is
  still loading.
* If the game runs as administrator, run the EXE as administrator too.
* `iw4x.exe` is still a **32-bit** client, so this x64 DLL cannot load into it —
  the launcher detects that and says so instead of failing silently.
* If the DLL is already injected it says so. Re-injecting cannot re-run it, so
  either press **F6** twice (off, then on again) or restart the game.
* **`closeWithGame`** in `[general]` controls what the window does after
  injecting: `1` (default) keeps it open while the game runs and closes it with
  the game; `0` prints the summary and waits for a keypress instead. It is never
  applied when output is redirected, so scripts don't hang.
* The F6 toggle covers the counter too, so `[drawfps]` disappears along with the
  rest when you toggle the unlocker off. Set `enabled=0` there if you never want
  it.

## Usage — the manual way

Prefer no self-extracting loader (see *Troubleshooting* for why you might)?
Use the loose files in `dist/`: `mw2_unlocker.dll` + `injector.exe`.

The DLL is configuration-driven and reads `unlocker.ini` from **its own
directory** (the DLL's path, not the game's) and writes `mw2_unlocker.log` next
to itself.

1. Edit `dist/unlocker.ini` (or `build/Release/unlocker.ini`) for your features.
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
| `LNK1104: cannot open mw2_unlocker.dll` | The DLL is loaded in a running game. Close MW2 and rebuild. |
| The EXE **vanishes** after it runs | Windows Defender quarantined it. The behavioural chain (an embedded PE dropped into `%LOCALAPPDATA%`, then a remote thread) matches a loader, so it was flagged `Program:Script/Wacapew.A!ml`. Restore it and add an exclusion for the folder, then report it to Microsoft as a false positive. Running the loose DLL + `injector.exe` instead avoids the self-extracting pattern. |
| Injector exits with `OpenProcess failed` | Game not running, wrong exe name, or the game runs elevated while you do not. Run the injector as administrator. |
| `LoadLibraryW returned NULL` | DLL bitness mismatch (must be x64), or a dependency failed to load. |
| Log says *could not find the cvar named ...* | The name does not exist in that build, or you misspelled it. Name lookups are case-insensitive, so casing is not the problem. |
| Log says *signature not found* | The signature does not match this build. Re-derive it (absolute addresses and displacements must be wildcards). |
| Patch applies but nothing changes | You are writing a copy rather than the source. Prefer `dvar_int` / `dvar_float` against the cvar; the engine owns that memory. |
| A value snaps back after a respawn | The engine re-initialises it. That is what `keepApplied=1` and a small `keepAliveMs` are for. |
| The requested FPS is not what the counter shows | The limiter quantises to whole milliseconds. See *The in-game FPS counter* above. |

## Anti-cheat warning

MW2's multiplayer uses **VAC**. Writing into a VAC-secured process or altering
game code in multiplayer can result in a **ban**. This project is intended for
**single-player, Spec Ops, and private/offline matches**. Do not use it on
VAC-secured servers. You are responsible for how you use it.

## Disclaimer

This is a reverse-engineering aid for a game you own, provided for educational
and personal use. It ships with tested presets for the specific 64-bit build
described in `docs/finding-signatures.md`; if the game is updated again, the
presets may need re-deriving. No game code or assets are distributed.
