# MW2 (2009) x64 FPS unlocker

### Download `MW2Unlocker.exe` & `unlocker.ini`

Start MW2, then run:

```
YOUR_DIRECTORY_OF_CHOICE\MW2Unlocker.exe
```

A window opens with a slider and a checkbox for the frame cap and for the field of
view, three sliders that move the first-person weapon, a film tweak card with six
sliders of its own, and an **Other Settings** card holding five switches - music,
fullbright, the HUD, the weapon model and the fog - together with the in-game
hotkey and a box for whether the window closes when the game does.

The two cards with sliders fold away, because their switches each head a stack of
values that means nothing while the switch is off: tick one and its rows appear and
the window grows to fit, untick it and they go again. A small triangle beside those
two switches points right while a card is closed and down once it is open. Change
what you want, then
press **Apply && save**: while the game is injected the change lands immediately,
and if it is not running yet the value is saved to `unlocker.ini` and applied when
the unlocker is injected.

The mouse sensitivity is deliberately **not** in the window. Writing that setting
was measured not to change the aim, so a slider for it only promised something that
did not happen. The `[sensitivity]` section is still in `unlocker.ini` if you want
to try it yourself: it writes the cvar and a line in the game's own settings file,

```
players\config_mp.cfg        seta sensitivity "3.45"
```

which the game reads when it starts. The game saves its settings over that file as
it exits, so the unlocker writes the line again once the game has closed.

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
