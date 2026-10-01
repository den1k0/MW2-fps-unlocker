# MW2 (2009) x64 FPS unlocker

### Download `MW2Unlocker.exe` & `unlocker.ini`

Start MW2, then run:

```
YOUR_DIRECTORY_OF_CHOICE\MW2Unlocker.exe
```

A window opens with a slider and a checkbox for the frame cap, for the field of
view and for the mouse sensitivity, plus the key that toggles it in game. Change
what you want, then press **Apply && save**: while the game is injected the
change lands immediately, and if it is not running yet the value is saved to
`unlocker.ini` and applied when the unlocker is injected.

The sensitivity slider is there because the game's own one shows no number, so
there is no way to set an exact value in game. It is switched off by default: it
is the only setting here that changes how the game plays rather than how it
looks.

It is also the one setting that is **not live**. The engine never reads the
sensitivity setting while the game is running - it copies it into your player
profile when it starts, and the aiming code reads the profile after that - so
Apply writes it into the game's own file as well:

```
players\config_mp.cfg        seta sensitivity "3.45"
```

That takes effect the next time the game starts. Because the game also saves its
settings over that file when it exits, the unlocker writes the line again once
the game has closed - otherwise the game's own value would win.

Press **F6** in game to toggle the patches on and off - or whatever key the
window is set to.

Worth knowing:

* It waits for the game (up to a minute), so you can launch it while MW2 is
  still loading. The status line at the bottom of the window says what it is
  doing.
* If the game runs as administrator, run the EXE as administrator too.
* If the DLL is already injected it says so. Re-injecting cannot re-run it, so
  either press **F6** twice (off, then on again) or restart the game.
* **`closeWithGame`** in `[general]` controls what the window does: `1` (default)
  keeps it open while the game runs and closes it when the game exits; `0` leaves
  it open so you can still change values after quitting.
* The window and `unlocker.ini` are the same settings: it reads the file when it
  opens and writes it when you press Apply, leaving your comments alone. An
  `unlocker.ini` next to the EXE takes priority over the stored copy under
  `%LOCALAPPDATA%`, so the file you can see is the file in use.


## Windows Defender fix (It might treat this as malware)

Windows Security -> Virus & threat protection -> Protection history
  -> find the detection -> Actions -> Restore

Windows Security -> Virus & threat protection -> Manage settings
  -> Exclusions -> Add -> Folder -> YOUR_FOLDER
