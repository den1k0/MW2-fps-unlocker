# MW2 (2009) x64 FPS unlocker

### Download `MW2Unlocker.exe`

Start MW2, then run:

```
YOUR_DIRECTORY_OF_CHOICE\MW2Unlocker.exe
```

![alt text][logo]
[logo]: https://github.com/den1k0/MW2-fps-unlocker/blob/main/preview.jfif "Logo"

A window opens with a slider and a checkbox for the frame cap and for the field of
view, three sliders that move the first-person weapon, a film tweak card with six
sliders of its own, and an **Other Settings** card holding five switches - music,
fullbright, the HUD, the weapon model and the fog - together with the in-game
hotkey and a box for whether the window closes when the game does.

Change what you want, then press **Apply && save**: while the game is injected the change lands immediately,
and if it is not running yet the value is saved to `unlocker.ini` in `%LOCALAPPDATA%` and applied when
the unlocker is injected.

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
