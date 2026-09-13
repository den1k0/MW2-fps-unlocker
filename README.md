# MW2 (2009) x64 FPS unlocker

### Download `MW2Unlocker.exe` & `unlocker.ini`

Start MW2, then run:

```
YOUR_DIRECTORY_OF_CHOICE\MW2Unlocker.exe
```

That is the whole procedure. `MW2Unlocker.exe` is **self-contained** — the
unlocker DLL and the default config are embedded as resources, so there is one
file to double-click and no arguments to remember.

Press **F6** in game to toggle the patches on and off.

Worth knowing:

* It waits up to 30 seconds for the game, so you can launch it while MW2 is
  still loading.
* If the game runs as administrator, run the EXE as administrator too.
* If the DLL is already injected it says so. Re-injecting cannot re-run it, so
  either press **F6** twice (off, then on again) or restart the game.
* **`closeWithGame`** in `[general]` controls what the window does after
  injecting: `1` (default) keeps it open while the game runs and closes it with
  the game; `0` prints the summary and waits for a keypress instead. This
  setting is never applied when output is redirected, so scripts don't hang.
  The launcher adds the key to an existing config automatically if an older
  version of the file is missing it.
* Edit `unlocker.ini` to change the FOV, the max FPS
  value, or the toggle key. Your edits are kept — the embedded default is only
  written the first time. Delete the file to get the default back.
* Keep `unlocker.ini` next to `MW2Unlocker.exe` and it takes priority, so the
  file you are editing is always the file that gets used.

## Settings

```ini
[fps]
value=250        frame cap. 0 = no cap at all (fastest)

[fov]
value=90         field of view, in degrees

[drawfps]
value=1          the game's own FPS counter on screen. 0 = off
```

Put `enabled=0` in a section to drop that tweak entirely.

### The in-game FPS counter

`[drawfps]` switches on the engine's own counter (`cg_drawFPS`). It is worth
having because an overlay like Steam's is **not** measuring the same thing: the
overlay counts the frames that reach the GPU, while the game counts the frames
its own loop produces. If an exact frame rate matters to you, trust the in-game
counter rather than the overlay.

The engine's limiter is also imprecise. It rounds your value down to a whole
number of milliseconds and then overshoots a little, so `250` lands around 212
and `167` lands around 190 — faster than asked for. For an exact frame rate, set
`[fps] value=0` and cap it with your graphics driver (NVIDIA Control Panel ->
Max Frame Rate) or RTSS instead.

## Windows Defender fix (It might treat this as malware)

Windows Security -> Virus & threat protection -> Protection history
  -> find the detection -> Actions -> Restore

Windows Security -> Virus & threat protection -> Manage settings
  -> Exclusions -> Add -> Folder -> YOUR_FOLDER

An EXE that unpacks a DLL and injects it looks exactly like a loader to a
heuristic scanner, which is all this detection is. If you would rather not add
an exclusion, `dist\mw2_unlocker.dll` + `dist\injector.exe` do the same job
without the self-extracting step.

## For developers

How every value was found — the `com_maxfps` integer, the `cg_fov` clamp, the
`cg_drawFPS` spelling, and the frame-limiter arithmetic — is written up in
[`docs/finding-signatures.md`](docs/finding-signatures.md). Build instructions
and the architecture are in [`docs/how-it-works.md`](docs/how-it-works.md).
