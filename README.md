# MW2 (2009) x64 FPS unlocker

### Download `MW2Unlocker.exe`

Start MW2, then run:

```
YOUR_DIRECTORY_OF_CHOICE\MW2Unlocker.exe
```

![alt text][preview]

[preview]: https://github.com/den1k0/MW2-fps-unlocker/blob/main/docs/preview.png "Preview"

A window opens with a slider and a checkbox for the frame cap, a slider for the
field of view, a viewmodel card whose left column moves the first-person weapon
and whose right column adjusts the HUD safe area and the compass size, a film tweak
card whose two columns carry three grade sliders on the left and the glow's four on
the right with the grade's invert switch under them, an **Other Settings** card
holding nine switches in three columns - music,
fullbright, the HUD, the crosshair, the weapon model, the fog, the numeric
ping and the third-person camera - together with
the two hotkeys side by side and, spanning the width under them, a box for whether
the window closes when the game does. Below that, a **Debug** card holds the two testing sliders — the
timescale and the prop gravity, which starts at -800 — behind one switch that
gates them and stays folded until it is ticked, and, on a second row beside each
other, blur (0.00 to 32.00) and the near clip plane (0.00 to 999.00), with the
sprint speed scale (0.00 to 2.00, the game's own 1.5) and an **MP paused** switch —
which stops the world for a clean screenshot — sharing the third row. The viewmodel, film tweak
and Debug cards all fold away like that, so the window is only as tall as the
settings it is showing. The strip along the top
carries the profile box as well: a dropdown of three slots, kept as files under
`%LOCALAPPDATA%\MW2Unlocker\profiles`. The window opens on Profile 1 and loads it
if it exists; choosing another loads that one, and **Apply & save** stores what the
window shows into the slot it is on, so a whole set of settings can be put back in
one go. **Next profile** is a hotkey, with its box beside the toggle key's: one press
of it in game does both halves at once — the DLL polls the key, asks the window for
the next slot, and the window loads and applies it — so a match can be switched
between profiles from the keyboard. Both hotkeys are global: they live in the working
config's `[general]` section, so no profile can move them. A line at the
top names the yellow it labels those settings in, so you can see at a glance which
ones are only re-read once you rejoin a match, a small dot
beside the status line at the bottom turns green once the game is answering, yellow
while it is still looking for it, and red if something failed, and the build number
sits beside the buttons — stamped afresh on every build — so you can say which
version you are on, with **Check for update** under it: it asks the repository
whether a newer build has been published and opens the download page if one has.

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
