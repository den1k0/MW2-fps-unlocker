# MW2 (2009) x64

## Usage — the easy way (single EXE)

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

## Windows Defender fix (It might treat this as malware)
Windows Security -> Virus & threat protection -> Protection history
  -> find the detection -> Actions -> Restore

Windows Security -> Virus & threat protection -> Manage settings
  -> Exclusions -> Add -> Folder -> YOUR_FOLDER
