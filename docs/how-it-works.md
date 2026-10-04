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
| `[general]` | `delayMs`, `toggleKey`, `closeWithGame`, `keepApplied`, `keepAliveMs`, `gameExe` | `toggleKey=0x75` is F6, and the window can change it live. `keepAliveMs` is how often the watchdog re-checks; `0` disables it. The hotkey takes the whole set of patches off and on again — and it brings back only what is switched on, so a setting left unticked in the window stays unticked. There is no `profile=` key any more: the window always opens on **Profile 1** and loads that file if it exists. |
| `[fps]` | `enabled`, `value` | `com_maxfps`. `0` = uncapped, `250` = a sensible ceiling, `1000` = effectively uncapped. |
| `[fov]` | `enabled`, `value`, `max` | `cg_fov` in degrees. `max=` raises the engine's own 80-degree clamp and doubles as its switch: `0` leaves the clamp alone. **Not in the window** — the clamp's checkbox was taken out, so `max=` is set here and is the only way past 80 degrees. Still applied if switched on in the file. |
| `[music]` | `enabled` | `enabled=1` writes `snd_enableStream 0`. The soundtrack is streamed audio, so this switches streams off; re-applied continuously because the game saves and reloads its sound settings. |
| `[r_fullbright]` | `enabled` | `enabled=1` writes `r_fullbright 1` — the world drawn unlit. Read from six places. |
| `[cg_draw2D]` | `enabled` | `enabled=1` writes `cg_draw2D 0` — no 2D overlay at all: the HUD, the crosshair, the on-screen counters. Read from three places. |
| `[cg_drawGun]` | `enabled` | `enabled=1` writes `cg_drawGun 0` — the first-person weapon is not drawn. Read from four places. |
| `[r_fog]` | `enabled` | `enabled=1` writes `r_fog 0` — no distance fog. Read once, from the view setup. |
| `[r_filmTweakEnable]` | `enabled` | `enabled=1` writes `r_filmUseTweaks 1` **and** `r_filmTweakEnable 1` — the film colour grade on. It is behind two gates and both are registered off; see *Music and the other switches*. |
| `[cg_gun_x]`, `[cg_gun_y]`, `[cg_gun_z]` | `enabled`, `value` | Floats: where the viewmodel sits — forward, right and up, in engine units — defaulting to 0 with no real clamp, read once each. One switch and three sliders in the window, and the same enabled flag goes into all three sections. |
| `[safeArea_adjusted_horizontal]`, `[safeArea_adjusted_vertical]` | `enabled`, `value` | The HUD safe area: the fraction of the screen the 2D overlay is laid out within, so a smaller number pulls the HUD towards the centre. Floats registered at 1.0 — the pair the game's own Options > Safe Area menu writes, and what `getadjustedsafearea*` returns. One switch and two sliders, in a second column of the viewmodel card. |
| `[safeArea_horizontal]`, `[safeArea_vertical]` | `enabled`, `value` | The base pair of the same family, registered at 0.85 and read by the code that builds the overlay rectangles. **Not in the window**: it was wired to sliders alongside the adjusted pair and moving it changed nothing on screen, so it was taken back out — the same treatment black level and the sensitivity got. Still applied if switched on in the file. |
| `[compassSize]` | `enabled`, `value` | `compassSize` — "Scale the compass", the size the compass strip is drawn at. A float registered at 1.0 with 0 as its minimum and `FLT_MAX` as its maximum, read from `+0x10` in ten places. A HUD size, so the window keeps it in the safe-area column — but **not** behind that column's switch: it has no gate of its own, so it is written whenever the window saves and applied with everything else, the way blur is, which also means unticking the safe area no longer hands the compass back to the game's own value. The slider stops at 5.00 because the engine offers no ceiling to follow. **0 crashes the game** — a scale of zero collapses the compass geometry and something downstream divides by it — so the slider and its number box stop at 0.1 and the live path floors the value again on the way out. The config file is not floored, and says so. |
| `[cg_drawCrosshair]` | `enabled`, `value` | An int registered at 1 — "Turn on weapon crosshair", cached at `0x809990` — and read once as a byte gate (`cmp byte ptr [rax + 0x10], 0`) over a block of HUD code, so any non-zero value draws it. The window's *Hide Crosshair* switch, in *Other Settings*, is inverted on purpose: enabling it writes the fixed 0, so `enabled=1` here means "hidden". |
| `[cg_scoreboardPingText]` | `enabled`, `value` | The scoreboard's ping *number*: an int registered with **0** — "Whether to show numeric ping value" — read at `+0x10` in two places (`0xE388E`, `0xE4625`) where the flag picks one of two layout tables. The window's *Numeric Ping* switch keeps its state in this section, and writes the value as 1. |
| `[cg_scoreboardPingGraph]` | `enabled`, `value` | The scoreboard's ping *bars*: an int registered with **1** — "Whether to show graphical ping" — read once as a gate (`0xE3E54`: `cmp byte ptr [rax + 0x10], 0; je <skip>`) over its drawing block. The same switch writes 0 here, because the number is drawn only where the graph is not; switching off restores both defaults. See *Music and the other switches*. |
| `[timescale]` | `enabled`, `value` | The game's own clock. A float registered at 1.0 and read as one (`movss` at `+0x1F427D`), cached at `0x1D26580`; 1.0 is neutral, below it slows the game down and above it speeds everything up. Gated by the *Debug* card's one switch, which is written to this section's `enabled` **and** to `[phys_gravity]`'s — and with the switch off the cvar is *restored*, so the game's own value comes back rather than the unlocker merely not writing. Its slider covers 0.50 to 5.00 in hundredths, and the floor is measured rather than the engine's: the registration passes 0.001 and nothing clamps the value, but below about 0.5 the game's own time sync fights the slowdown and rubberbands, so the window starts where the engine still delivers. TESTING. |
| `[phys_gravity]` | `enabled`, `value` | "Physics gravity in units/sec^2." — the gravity on *objects* rather than on the player. A float registered through the float helper at 800, cached at `0x1B53BE0`; 800 is neutral and a negative number inverts it. Gated with the timescale by the *Debug* card's one switch, and restored when that is off: a slider over whole units from -2000 to 2000 that starts at **-800** — the same magnitude as the engine's own with the sign flipped, which is the end of the range worth trying in a match; 800 is what to set to put the game back as it was. Not `g_gravity`: the multiplayer binary does not register that name at all — it is in `iw4sp.exe` at `0x75FC6` — so this is the multiplayer equivalent. TESTING. |
| `[camera_thirdPerson]` | `enabled` | "Use third person view globally". An int registered in the block that also holds `bg_forceDualWield` and `camera_thirdPersonOffset`, cached at `0x58D790`, and read by code as a gate in four places — `0x8A696` is a predicate that answers whether third person is forced, returning false both while the dvar is 0 and when the entity's state carries the first-person flags. The window's *Third person view* switch, in *Other Settings*, writes 1 and hands the engine's own value back when it is not ticked. Off by default: it changes what the game draws. |
| `[player_sprintSpeedScale]` | `enabled`, `value` | "The scale applied to the player speed when sprinting". A float registered at **1.5** between 0 and **2.0** (rva `0x8C17B`; the two constants are at `0x3CA870` and `0x3CA980`), cached at `0x58D728` and read twice — `0x8F7F5` and `0x90678` — each `mov rax, [cache]; mulss reg, dword ptr [rax + 0x10]`. So it *scales* a value already on its way into the movement code rather than replacing it, and a live write is enough. **No gate**, like blur: the section is always on and only the value moves. The window's *Sprint speed* slider is the third row of the *Debug* card at the foot of the window, covering the registration's whole 0.00 to 2.00; it starts at the engine's own 1.50, so a slider nobody touches writes what the game already has — and note that 1.00, which reads like "normal", is two thirds of the speed the game actually ships. |
| `[mp_paused]` | `enabled` | "If true ignore server time advancing.  Handy for taking hi-resolution screenshots without the world moving". An int registered at **0** in the multiplayer binary only, cached at `0x5AF6C8` and read once (`0xB81CB`). **No gate**, so the window's *MP paused* switch, beside the sprint scale on the Debug card's last row, is the flag itself: on writes the fixed 1, off hands the engine's own 0 back. |
| `[bg_forceDualWield]` | `enabled` | "Force akimbo for all possible weapons". An int registered at **0** (registration rva `0x8B215`, flags `0xC`), cached at `0x589C18`, read in three places — `0x99D8A`, `0x18837E`, `0x18849A`. **Tried, and discarded**: the window offered it as a *Force dual wield* switch for a build, and the switch moved nothing in a match — the dvar is read, but not anywhere that changes a match — so the control has been taken out. The section is file-only now, so a value set here is still written. The name has to be spelled with the capital W, because the unlocker finds a dvar by its exact name string. |
| `[r_znear]` | `enabled`, `value` | "Things closer than this aren't drawn.  Reducing this increases the depth precision" — the near clip plane. A float read once at `0x24440`, inside the view-setup function that also reads `r_subwindow`, as `movss xmm0, 0.01; mov rax, [cache]; maxss xmm0, dword ptr [rax + 0x10]`, so the value is **floored at 0.01**: the window's 0.00 behaves as 0.01. The same block can override the near plane with a per-view value the caller passes (`[rbx+0x38]`, when it is above zero), and that is the view that uses `r_znear_depthhack` — "Viewmodel near clip plane", default 0.1 — so a large `r_znear` clips the world while the viewmodel's own near stays put. The registration's own limits are 0.001 and **10000** (loaded from rva `0x3CA70C` and `0x3CAB0C`), so the window's 0.00-to-999.00 range sits well inside them; 100 was the window's ceiling rather than the engine's. **No gate**, like blur: the section is always on and only the value moves. The window's *Z near* box is the second row of the *Debug* card, beside blur, and it starts at 4.00, the engine's own value, so an untouched box writes what is already there. The row rides the card's switch, so the box is only in reach with the card open, while the value itself is written either way. |
| `[r_filmTweakInvert]` | `enabled` | "Tweak dev var; enable inverted video" — an int registered at **0** (cached at `0x8CF7A20`, flags `0x40`) and read once, where it flips the grade's colour. **No gate**, so the window's *Invert the grade* switch is the flag itself: ticking it writes `enabled=1` (the `value` of 1) and unticking restores the engine's own 0. The switch is the fourth row of the film tweak card's left column, level with the glow column's *Desat* slider, and it rides the film tweak switch like the grade's rows do, so it appears only while *Enable the film tweak* is on. TESTING. |
| `[cg_footsteps]` | `enabled` | "Play footstep sounds" — an int registered at **1** (cached at `0x809BB0`) and read as a gate in four places. Inverted on purpose, like `[cg_drawCrosshair]`: the value written is 0, which makes it silent — a section writing 1 would write what the engine already holds and change nothing. **Not in the window**: it was the *Mute Footsteps* switch for a build, was tried, and did not earn a permanent control, so it is file-only now, applied if switched on here. TESTING. |
| `[ui_debugMode]` | `enabled` | "Draw ui debug info on the screen." — an int registered at **0** (cached at `0x65FEE68`) and read as a gate five times in multiplayer and five in single player. **Not in the window**: it was the *UI debug* switch for a build, was tried, and did not earn a permanent control, so it is file-only now, applied if switched on here. TESTING. |
| `[r_filmTweakContrast]`, `[r_filmTweakBrightness]`, `[r_filmTweakDesaturation]`, `[r_filmTweakLightTint]`, `[r_filmTweakMediumTint]`, `[r_filmTweakDarkTint]` | `enabled`, `value` | The grade's six parameters, registered at 1.4, 0, 0.2 and 1.1, 0.9, 0.7. The first three have sliders in the window, and are only read while the grade itself is on; the three tints are file-only now, having lost their sliders when the card was compacted. The tints are *colour* dvars — the section writes one grey level into each of their three colour components — and `r_filmTweakInvert` is a flag, so it is a switch of its own rather than a slider here. |
| `[r_glow_allowed]`, `[r_glow]`, `[r_glowUseTweaks]`, `[r_glowTweakEnable]` | `enabled`, `value` | The glow tweak's four gates, each written as 1 by the one switch in the window. All four ship at 0 apart from `r_glow`, so the bloom parameters are inert until every one of them is open — the film tweak's two-gate trap, one level deeper. |
| `[r_glowTweakRadius0]`, `[r_glowTweakBloomIntensity0]`, `[r_glowTweakBloomCutoff]`, `[r_glowTweakBloomDesaturation]` | `enabled`, `value` | The bloom itself, registered at 5, 20, 0.5 and 0. Sliders in the window, and turned up far they wash the screen out completely. |
| `[r_blur]`, `[r_blacklevel]` | `enabled`, `value` | "Dev tweak to blur the screen" and "Black level (negative brightens output)". Floats registered at 0, the second of them between -0.99 and +0.99. **No gate** — nothing in the game switches them and 0 is neutral — so the window writes both whenever it saves. Blur is confirmed working and carries a slider at the foot of the glow column, covering the engine's whole range, 0.00 to 32.00: that ceiling is the registration's own maximum (the float helper's `xmm3`, loaded from rva `0x3CAA74`) and its only reader at `0x637D5` merely tests the value against zero and squares it into a length, so neither the engine's `set` nor the renderer clamps it at run time either. Black level has no slider, because only two of its four references are real value reads (`+0x30B14`, `+0xF1790`) and both sit in a set-up path rather than the frame loop — a slider for it changed nothing visible, so the section is file-only, like `[sensitivity]`. |
| `[sensitivity]` | `enabled`, `value` | `sensitivity`. **Not in the window** — writing the cvar was measured not to change the aim in game, so the slider was removed rather than left promising something that does not happen. Still applied if switched on here: the cvar *and* a `seta sensitivity "..."` line in the game's own settings file. Off by default because it changes how the game plays. |
| `[drawfps]` | `enabled`, `value` | `cg_drawFPS`, an enum: 0 Off, 1 Simple, 2 SimpleRanges, 3 Verbose, 4 Verbose+Viewpos. **Single player only** - see below. |
| `[netfps]` | `enabled`, `value` | `sv_network_fps`, the only counter multiplayer reads. It is the network rate, not the frame rate. |
| `[lagometer]` | `enabled`, `value` | `drawLagometer`. Registered in multiplayer and read by nothing, so it does nothing; the switch is kept so that claim can be re-tested. |

Two more sections were here for a build and are gone, having been tried in a match:
`[subwindow_left]` with its three siblings — the four components of `r_subwindow`,
one section per component because a section writes one value into every offset it
lists — and `[r_debugShader]`. Both did exactly what the analysis predicted: the
subwindow scales and shifts the frame the renderer draws, and the shader mode draws
the shaders' own basis vectors. Both were dropped because neither is something to
play with. The sections are no longer understood, so a config that still carries them
does nothing; what the analysis found is kept in `docs/feature-report.md`.

The window covers the frame cap, the field of view, the viewmodel offsets and the
HUD safe area with the compass size, the film tweak's three grade sliders, the
glow tweak's four, and — under *Other Settings* — a three-by-three grid of nine
switches, with the two hotkeys side by side above them (*Toggle key* on the left,
*Next profile* on the right). *Close with Game*, the last of the nine, is a
launcher setting rather than a cvar: it says whether the window closes when the
game does. Four of the
rows in
that card are drawn in a plain yellow rather than in the body text's off-white — the
*Disable Music* and *Fullbright* switches and the safe area's *Horiz. adj.* and
*Vert. adj.* labels — because the game only re-reads them when a match is set up.
The note above the first card names that colour and draws the word itself in it,
which is why the note is the window's one owner-drawn static. Below that, in a card
of its own, is the card at the foot, *Debug* — the card that used to be called
*Server*. It carries one switch and three rows: the timescale and the prop gravity
(labelled *Prop Gravity*, since it is the gravity on objects rather than on the
player) behind **one switch that gates them**, and, beside them, blur — moved in
here from the glow column — and the near plane; the last row holds the sprint
scale, moved down from *Other Settings*, with the *MP paused* switch beside it,
which stops the server's clock advancing so a hi-resolution screenshot is not
spoiled by the world moving. Folding the card hides all three rows; the switch is a
real gate for the two testing settings, while blur, the near plane, the sprint
scale and *MP paused* are written either way because none of them has a gate to be
closed. The bottom of the template was re-measured for the new rows.
The counters are file-only for now, which is why the window
reads them without rewriting them: pushing the values back must not switch off
something the file turned on. Read the sensitivity row before enabling it — it is
file-only too, and for a better reason: it does not work. Black level is file-only
as well, because its value is not consulted per frame.

### Profiles

The strip above the frame cap carries a dropdown, and it is the only part of the
window that deals in more than one setting at a time. There are three slots —
*Profile 1* to *Profile 3* — and each is an ordinary `unlocker.ini` of its own
under `%LOCALAPPDATA%\MW2Unlocker\profiles`, so a profile can be edited by hand,
copied, or handed to somebody else like any other config.

| Action | What it does |
|---|---|
| **Choosing a slot** | Loads it into the window: every setting in it at once, through the same code start-up uses, so nothing is left over from the settings that were there before. The window is then laid out again, because the switches a profile carries decide which rows are showing. A slot that has never been used has no file yet, and choosing it says so rather than inventing values. |
| **Apply & save** | Is what stores them: it writes the working config as always, and then the window's values into the slot it is on. A slot's first save starts the file as a copy of the working config, so it carries the comments and any section the window does not offer. |
| **Next profile** | Is a *key*, not a button: an in-game hotkey that moves to the next slot, loads it and then does what **Apply & save** does, so a match can be switched between profiles from the keyboard. It wraps: 1, 2, 3, 1, and the dropdown is moved with it, because the dropdown is what shows which slot is in use. |

Loading a slot does not apply anything on its own — press **Apply & save** for that,
as with any other change. The next-profile key is the exception: it loads *and*
applies, which is the whole point of it. The window always opens on **Profile 1** and
loads it if the file exists, so that slot is the setting the tool starts from;
delete the file, or rename another profile over it, to change what that is.

**The two hotkeys are not part of a profile.** The toggle key and the next-profile
key live in the working config's `[general]` section, so switching profile leaves
both where they are — a profile file does not carry them at all (the window leaves
them out when it writes a slot), and loading a slot re-reads them from the working
config. The alternative — a key per profile — was rejected for the obvious reason:
a key that changes when you switch profiles is a key you cannot use to switch
profiles. They are set by the two boxes under *Other Settings*, side by side —
*Toggle key* on the left and *Next profile* on the right — which step through
F1..F12, and Apply writes the lines `toggleKey=` and `nextProfileKey=` in
`[general]`.

The next-profile key also needs both halves of the tool to be talking, and the
split is deliberate: the key is polled **inside the game's process** by the DLL,
because that is the only place a keypress is certainly visible, and the DLL then
posts a registered message to the launcher's window — it cannot switch profiles
itself, because the launcher is what owns the profile files. The launcher answers by
loading the next slot and applying it, which is the same code path as the *Apply &
save* button and therefore also what pushes the new values into the running game.
So one keypress is: key → DLL → message → launcher loads and applies → live channel
→ game. If the launcher is not open the key logs that it had nobody to ask, and if
the game is not injected the launcher's Apply simply saves the file.

The viewmodel, film tweak and *Server* cards fold away. Each has a switch directly
under its title and a stack of sliders under that, and the stack is worth nothing
while the switch is off, so it is hidden and everything below is pulled up — the
window is only as tall as the settings it is showing, and its bottom edge follows,
so there is no field of empty background under the buttons. The switch itself never
moves: it is what opens the card, so the rows come and go underneath it, and it
carries a small triangle pointing right while they are folded away and down once
they are out.

The viewmodel and film cards carry two switches and two columns of rows, so a card
stays open while *either* of its switches is on. The viewmodel card has the three
offsets on the left and the safe area's two sliders with the compass under them on
the right — three rows against three, which is why the card is the same height as
it was before the safe area was added; the film card has the grade on the left and
the glow on the right, and the *Invert the grade* switch is the left column's fourth
row, level with the glow column's *Desat* slider. Ticking one
switch alone brings its column out and leaves the other folded. The compass
under the safe area is the same shape: it has no gate of its own either, so
ticking the safe-area switch only brings its slider into reach — its value is
written and applied either way. Blur has no gate either, but it no longer lives
in this card: it moved to the Debug card, so its slider and the near plane's box
are both in reach whenever that card is open.

The Debug card is the simple case: one switch and two rows, and the switch is a
real gate rather than only a disclosure — with it folded the timescale and the
gravity are *restored* to the game's own values, not merely
left unwritten, while blur and the near plane are written either way because
neither has a gate to be closed. It starts folded,
so the card is one switch high until it is wanted; and because the profile the
window opens on carries that switch like any other setting, a slot saved with it
on brings the card out with it.
`tools/smokegui.ps1 -Click 1031,1023` toggles the viewmodel and film switches from
outside and prints the window's size after each click (541x676 with the film folded,
541x779 with it out), which is how the layout is
checked without anyone watching; `-Click 1093` does the same for the Debug card
(541x676 folded, 541x753 open), and `-SendNextProfile` posts the registered message
the DLL posts for the next-profile key, which is how the launcher's half of that key
is driven from outside - the half inside the game cannot be reached from a script.
`-SetEdit "1098=150" ... -ClickApply` types into a
number box the way a user does — set the text, then the notification that the box
lost the focus — which is the only path a typed value takes.

The small circle beside the status line at the foot of the window says what the
unlocker is doing at a glance: green once it has injected and the game is
answering on the live channel, amber while it is still looking for the game or
waiting for the channel to appear, red when an injection failed or the game did
not answer a change. The window paints it itself — it is not a control — and the
template insets the status text to leave room for it. Because the status line is
an ordinary control that is pulled up when a card folds, the paint handler moves
the dot by the same amount, which is what keeps the two together.

Beside the buttons, at the foot of the window, is the build number —
`build 46   2026-10-03`. In the window rather than in the title bar, because the
title belongs to the window manager and it is the window that gets screenshotted.
It is stamped **at build time**, by `cmake/bump_build.cmake`, from a counter kept
in the build tree: the counter is seeded from the commit count and then goes up by
one on every build, so two binaries from the same afternoon cannot claim the same
number — the commit count on its own did exactly that, which is why it was
replaced. The date is the builder's own local date rather than UTC, so it matches
the day they were having rather than yesterday. The short hash rides along, with a
`+` when the tree had uncommitted changes.

Underneath it is **Check for update**. It asks the repository - `version.txt` on
the main branch, fetched over HTTPS - what the newest published build is, and says
what it found: *Up to date (build 44)*, *Update available: build 51 - click to
open*, or the reason it could not tell. Once a newer build is known, the next
click on that same button opens the download page in the browser.

It deliberately does **not** download or replace the running EXE. A program that
fetches a new binary and overwrites itself with it is exactly the behaviour
Windows Defender already flags this tool for, so the click it would save is not
worth that. The check runs on a thread of its own, so the window never freezes
waiting for GitHub, and it gives up after five seconds.

The number comes from GitHub's contents API rather than from
`raw.githubusercontent.com`. The raw host is a CDN that keeps every response for
five minutes and ignores a cache-busting query string, so a check made just after a
release would report the previous build and look broken; the API's own answers are
allowed to be a minute old, which is the trade it makes for being current enough.
Either way the button reports what it read rather than guessing, and says which
build the repository is on even when that is not the one you have.

Publishing a build therefore has one extra step: set `version.txt` to that build's
number and commit it with the new EXE. Nothing else has to be kept in step - the
number in the file is compared with the one the EXE was stamped with, and both come
from the same counter in the build tree. A repository that has not published the
file yet answers 404, and the button says so rather than looking broken.

Above the first card is the one other piece of text: *"Some settings take effect
only after you rejoin a match."* Several of these cvars — the safe area, the
compass, the HUD switches — are read when a match is set up rather than every
frame, so a change made mid-match does nothing until the next one starts. The note
is drawn in a font three quarters the size of everything else, from the same base
font the card titles are made from, so it reads as advice rather than as one more
setting.

The status line sits in a box of its own at the foot of the window, beside the
buttons: the window paints it, the way it paints the cards, and moves it with the
status line when a card folds. The note and the build number are the other two
pieces of text that sit on the window rather than inside a card, and they take the
*window* brush when the control asks what to paint behind itself, so they are text
on the background rather than a grey rectangle whose left edge lines up with
nothing. The status line, being inside that box, takes the card brush and blends
into it instead.

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

## Mouse sensitivity

**This setting is not offered in the window.** Writing the cvar was measured not to
change the aim, so the slider was taken out rather than left promising something
that does not happen. The `[sensitivity]` section stays in the config, so the
writes it does make — the cvar, and the game's own settings file — still happen
when it is switched on there.

What is interesting is that the code says it should work, which is why it was
worth chasing at all. Two things had to be right: finding the cvar, and the cvar
being the one the aim reads.

The binary holds **two** strings that a
case-insensitive search for `sensitivity` matches:

| rva | string | what it is |
|-----|--------|------------|
| `0x377F6C` | `Sensitivity` | a profile field label, pointed at by nothing |
| `0x379638` | `sensitivity` | the cvar name, referenced by its registration |

The cvar locator used to take the first match, and if nothing pointed at that
string it reported the cvar as missing. It landed on the label, found nothing
pointing at it, and gave up — so every write was skipped, the log said
`no pointer to that name was found in the module`, and the setting behaved
exactly like a feature that does nothing. Name lookups now collect *every*
occurrence, rank the ones that match the case exactly and begin a string first,
and keep the first one a `dvar_t` actually points at.

The ranking can be checked on any copy of the binary from the command line:

```powershell
python tools\disasm.py iw4mp.exe --candidates sensitivity
```

```
candidates for 'sensitivity', in the order a lookup tries them:
  1. rva 0x379638  exact case, starts a string (this is what a cvar name looks like)
  2. rva 0x379626  exact case, but inside a longer string
  3. rva 0x377F6C  different case, inside a longer string
  4. rva 0x3781BF  different case, inside a longer string
```

Number 1 is the cvar. Number 3 is the label the old lookup took, and number 4 sits
inside `profile_setViewSensitivity`; neither begins a string, so neither can be the
name of anything — which is exactly the test that had to be applied and was not.

The aim path, for the record (`sub_F6C80`, the mouse-look function):

```
x = mouse dx          (averaged with the previous sample when m_filter is on)
y = mouse dy
factor  = (sqrt(x*x + y*y) / max) * cl_mouseAccel->value + sensitivity->value
factor *= cl->[0x31EC]
outX = x * factor
outY = y * factor
```

The dvar identity was confirmed the other way round as well: asking which cvar a
cached pointer belongs to (`--static 0xD05020`) walks back through the enclosing
function to the registration and names it. `0xD05010`, which the first pass here
took for `sensitivity`, is `cl_packetdup`; the real one is `0xD05020`.

**And it still does not work.** With the lookup fixed so the right `dvar_t` is
found, writing that value changes nothing in game, even though the code above
reads it on every frame. The honest summary is that the value the aim obeys is
somewhere else again — the game keeps its own copy — and the only thing observed
to move the aim is the game's own settings file:

    <game folder>\players\config_mp.cfg        seta sensitivity "3.45"

The game reads that at startup. Editing the line by hand and starting the game
does change the aim, so that is the path that works; it just needs a launch. The
unlocker writes the line for you when `[sensitivity] enabled=1`, quoted the way
the game quotes it, and writes it again once the game closes, because the game
saves its own settings over that file on exit.

The profile field is a dead end for a different reason. The game's own
`profile_setViewSensitivity` command writes `profile[0].viewSensitivity` at rva
`0x6CCCC4`, and an xref of that address finds only the command itself and the
defaults initialiser. Nothing reads it, so nothing downstream of it can be what
the aim follows either.

## Music and the other switches

`snd_enableStream` is the switch the sound code tests before it starts a streamed
sound, and the soundtrack is streamed — so holding it at `0` is what keeps the
music away. Three places read it, every one of them as a gate:

```
mov rax, [snd_enableStream]
cmp byte ptr [rax + 0x10], 0
je  <skip the stream setup>
```

The guides say to follow it with `snd_restart`. In this build that does nothing:
`snd_restart` is registered in the command table against rva `0x80E50`, which is
`C2 00 00` — `ret 0`, a function that returns immediately — and no other code
refers to the name, so nothing is left that could react to it. There is no
`snd_restart` dvar either. So the setting has no "restart" step, and it is simply
kept applied instead: the game saves and reloads its sound settings with the rest,
a single write does not survive, and the watchdog is what makes this stick.

The soundtrack is not the only streamed audio in the engine, so anything else
that streams is affected too: this is "no streamed audio" rather than a
music-only switch.

`r_fullbright`, `cg_draw2D`, `cg_drawGun` and `r_fog` share that card in the
window (the film tweak has one of its own, because it turned out to need two
cvars and a longer explanation than a row could hold). They are all the same kind
of thing: a cvar whose value *is* the setting, so the switch writes a fixed
number and there is nothing to drag.

*Numeric Ping* is the one switch in that card which writes **two** cvars, and the
reason is that the scoreboard's ping display is a pair with opposite senses:
`cg_scoreboardPingText` is registered 0 ("Whether to show numeric ping value") and
`cg_scoreboardPingGraph` is registered 1 ("Whether to show graphical ping"), so a
match draws bars and no number. Ticking it writes text 1 and graph 0 — the number
is drawn only where the graph is not — and unticking restores both of the engine's
own values. It is off by default because it changes how the scoreboard reads. It
also sits alone in the card's third column, which the shortened names made room
for, and that is what took the card back to three rows.

| switch | cvar | writes | read from |
|--------|------|--------|-----------|
| Disable Music | `snd_enableStream` | `0` | 3 places |
| Fullbright | `r_fullbright` | `1` | 6 places |
| Hide HUD | `cg_draw2D` | `0` | 3 places |
| Hide Weapon model | `cg_drawGun` | `0` | 4 places |
| Disable Fog | `r_fog` | `0` | 1 place |
| Enable the film tweak | `r_filmUseTweaks` + `r_filmTweakEnable` | `1` + `1` | 1 place each |

They go through the same watchdog as everything else, because the game reloads
its own settings and would otherwise put them back - the HUD and the weapon model
in particular, which are re-read when a level loads.

The viewmodel offsets are the one place in the window with three sliders and one
switch, and they are three *values* rather than a gate: `cg_gun_x`, `cg_gun_y` and
`cg_gun_z` are the forward, right and up position of the first-person weapon, all
defaulting to 0 and clamped by nothing (the registration passes `-FLT_MAX` as the
minimum), each read once where the viewmodel origin is built. The window offers
-10.00 to +10.00 in hundredths, which is the range's own choice rather than the
game's, so the config accepts anything if you want to go further. Ten units is
already well past the point where the weapon leaves the screen.

The film tweak's parameters are sliders rather than switches, because the grade
is worth tuning and not only switching: `r_filmTweakContrast` (1.4),
`r_filmTweakBrightness` (0), `r_filmTweakDesaturation` (0.2) and the three tints
(1.1, 0.9 and 0.7) are the values the game registers them with. Unlike the
viewmodel offsets, these ranges follow the domains that make sense for a grade
rather than being widened for their own sake: contrast 0.00 to 3.00, brightness
-1.00 to 1.00, desaturation -1.00 to 2.00 and the tints 0.00 to 2.00. A negative
desaturation saturates rather than drains, which is why its span is lopsided, and
the tints stay positive because below 0 they invert the channel instead of dimming
it. This file still accepts anything outside those ranges — the values are written
straight into the cvar's memory rather than through the game's setter.

The three tints needed a note of their own. They are *colour* dvars: the game
registers each one with four floats — red, green, blue and an unused fourth —
through the helper that gathers four values into a 16-byte block and passes a
pointer onwards, and the dvar's type byte is 9 rather than the 4 or 6 the scalars
use. A single float cannot express a colour, so the sections list the first three
components in `valueOffsets`:

```ini
[r_filmTweakLightTint]
type=dvar_float
cvar=r_filmTweakLightTint
valueOffsets=0x10,0x14,0x18
value=1.1
```

Every offset in that list is handed the same bytes, so the number becomes r, g
*and* b — a grey tint, which is exactly what the game's own defaults are. A real
colour would need three different numbers, and the feature engine carries one.

Not a slider: `r_filmTweakInvert` is a flag rather than a value.

Note which way round the film tweak goes. Everything else here switches something
*off*, so the muscle memory is to write 0 — but this one the game registers as
already off, so writing 0 is a no-op and writing 1 is the setting. That comes from
the registration itself: `xor edx, edx` for the default, then the call.

It also needs *two* cvars, which is what made the first attempt look like it did
nothing. The renderer copies the `r_filmTweak*` family into its per-frame struct
only when `r_filmUseTweaks` is on:

```
rva 0x2127B  mov rax, [r_filmUseTweaks]
rva 0x21282  cmp byte ptr [rax + 0x10], 0
rva 0x21286  je  <skip the whole copy>
rva 0x2128C  mov rax, [r_filmTweakEnable]      ; only reached if the above is on
rva 0x2129E  mov [rbx + 0x220], cl             ; the flag, copied per frame
```

So with `r_filmUseTweaks 0`, `r_filmTweakEnable` is read by nothing at all: it is
the outer gate that decides whether the inner one is looked at. The switch writes
both, and the three grade sliders underneath are written too — they start at the values
the game itself registers, so a slider left alone changes nothing, and one that is
moved does.

The read counts are the part worth checking before adding a switch of your own: a
cvar that is registered but never read is written and ignored. In the multiplayer
build that is `cg_drawFPS`, `cg_drawFPSLabels`, `drawLagometer` and
`cg_drawViewpos` — every one of them registered, none of them read, so none of
them can be a switch. `cg_drawViewpos` is the clearest case: it has exactly one
reference in the whole binary, which is the store that caches its own pointer.
The six above are all read, which is why they are offered and those are not.

## Repository layout

```
CMakeLists.txt          Build (DLL + injector + one-click launcher, x64 enforced)
cmake/bump_build.cmake  Stamps the per-build number into the generated build_info.h
version.txt             The newest published build number, read by "Check for update"
config/unlocker.ini     Feature configuration (embedded into the launcher, copied next to the DLL)
src/log.*               Minimal file + debug-output logger
src/memory.*            Module range lookup, safe reads/writes, page iteration
src/scanner.*           AOB signature parser + scanner + ranked case-insensitive name search
src/patcher.*           Register patches, apply / restore / toggle / re-apply
src/config.*            Tiny INI parser
src/features.*          Feature engine (presets, dvar lookup, live updates, float_ptr, bytes)
src/dllmain.cpp         Entry point, worker thread, hotkey, control window, exported API
src/ipc.h               The live-channel protocol the launcher and the DLL share
injector/main.cpp       x64 LoadLibrary injector
launcher/main.cpp       Launcher plumbing: payload, process lookup, injection, ini editing
launcher/gui.cpp        The control window: dialog, custom slider, owner-drawn controls
launcher/update.*       The HTTPS "is there a newer build?" check (WinHTTP)
launcher/app.h          Interface between the window and the plumbing
launcher/build_info.h.in  The build-number template, stamped at build time
docs/finding-signatures.md  The full reverse-engineering write-up
dist/                   Built, ready-to-run output
tools/                  Helper scripts used during the investigation
tools/disasm.py         Disassemble an RVA with its operands resolved, follow a cvar to its
                        readers, print the function around an address, name a cached dvar
                        pointer (--xref, --dvar, --function, --static, --qwords)
tools/deadcvar.ps1      Is a cvar read by this build at all?
tools/xref.ps1          Code references to an address, in raw bytes
tools/callers.py        Call/jmp targets of an address: the references xref cannot see,
                        because a function is reached by `call rel32` and not by a pointer
tools/dumpatrva.ps1     Bytes at an RVA, plus the section table
tools/findstrings.ps1   Strings in a binary
tools/smokegui.ps1      Drive the control window from outside: -ClickApply for the
                        write path, -Click <ids> for the collapsible cards
tools/smokelive.ps1     Inject into any 64-bit host and push a live update, no game needed
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

## The tools

The scripts in `tools/` are where the numbers in these docs came from. They read
a binary **as a file**, so nothing has to be running and no debugger is attached -
which matters, because attaching to a live multiplayer session is exactly what
the warning at the top of [`finding-signatures.md`](finding-signatures.md) is
about.

```powershell
pip install capstone
python tools\disasm.py iw4mp.exe --dvar sensitivity
python tools\disasm.py iw4mp.exe --function 0xF6C80
python tools\disasm.py iw4mp.exe --static 0xD05020
python tools\disasm.py iw4mp.exe --xref 0x1406CCCC4
```

That is, in order: follow a cvar from its name to the statics that cache it and
the code that reads it; print the whole function containing an address; say which
cvar a cached pointer static belongs to; list every code reference to an address.

The `.ps1` scripts are blocked by the default PowerShell execution policy. Run
them as:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\xref.ps1 -Exe iw4mp.exe -Target 0x1406CCCC4
```

## Usage — the easy way (single EXE)

Start MW2, then run:

```
dist\MW2Unlocker.exe
```

That is the whole procedure. `MW2Unlocker.exe` is **self-contained** — the
unlocker DLL and the default config are embedded as resources, so there is one
file to double-click and no arguments to remember. On start it:

1. extracts the DLL and config to `%LOCALAPPDATA%\MW2Unlocker\`,
2. opens the control window and looks for the game in the background,
3. injects as soon as `iw4mp.exe`, `iw4sp.exe` or `iw4x.exe` appears, and says so
   in the status line.

The window has a slider and a checkbox each for the frame cap, the field of view
and the mouse sensitivity, and a box for the in-game toggle key (F1-F12, F6 by
default). **Apply && save** writes `unlocker.ini` and, when the unlocker is
already injected, pushes the values into the running game - see *The live
channel* below. Nothing is written while you drag, so the game never sees a
half-changed state.

Sensitivity is the odd one out among those, in two ways.

It is a float, and the slider works in hundredths (1.00 to 20.00) so an exact
value can be set - the game's own slider shows no number at all, which is the
whole reason for offering it here. It is switched off unless asked for, because
it changes how the game plays rather than how it looks.

And it is **not live**, which is worth explaining because everything else in this
window is. The one reference to the `sensitivity` dvar in `iw4mp.exe` is the dvar
system's own hash probe - the code around it is bucket arithmetic (`sub ecx,
[rax+0x10]`, `and rdx, rax`, a table stride) - so the aiming code never consults
that dvar at run time. What it reads is the *player profile*: the engine copies
the setting into the profile when the game starts (hence `viewSensitivity` and
`profile_setViewSensitivity` in the binary) and the input path uses the profile
from then on.

That is why `seta sensitivity` in `players\config_mp.cfg` works and a live dvar
write does nothing. So Apply does both: it pushes the value like any other
setting, which is harmless and may yet matter to the single-player client, and it
writes the line into the game's own settings file - quoted exactly the way the
game writes its own lines, since matching that format is what keeps the parser
happy:

```
<game folder>\players\config_mp.cfg      seta sensitivity "3.45"
```

The file is the part with an effect, and it needs the next launch.

There is a second half to that, learned by watching the file: the game **saves its
settings over it when it exits**, so a line written during play is undone the
moment the player quits - which is what makes this look like a feature that does
nothing. The worker therefore writes the line again after the game has closed,
re-reading the value from the config rather than from the window. By hand the
same rule applies: edit the file while the game is closed.

The on-screen counters have no controls yet; they are still set in the file:

```ini
[drawfps]
enabled=1
value=1
```

The window is drawn by the launcher rather than assembled from stock controls.
The reason is not decoration: the common controls trackbar **cannot be
recoloured** - it is painted by the theme - so on a dark window it looks like a
light grey strip left behind by another program. `gui.cpp` therefore draws its
own slider and hotkey box, and owner-draws the checkboxes and buttons. The
palette is the one the game itself is built from: olive greys for the surfaces,
orange for anything that wants attention, green for anything switched on.

Owner-drawing has one consequence worth knowing, because it caused a bug and a
half: an owner-drawn button keeps no check state of its own, since
`BS_OWNERDRAW` *replaces* `BS_AUTOCHECKBOX` rather than adding to it. The check
states therefore live in the window's state, and `IsDlgButtonChecked` is never
asked. Tab order, focus, arrows, Enter and Escape still come from the dialog.

Press **F6** in game to toggle every patch on and off at once.

Worth knowing:

* An `unlocker.ini` sitting **next to the EXE** wins over the stored copy. The
  window reads that file when it opens, and writes both it and the working copy,
  so the two cannot drift apart. This matters because the working config lives
  under `%LOCALAPPDATA%`, and edits to a forgotten copy would silently do
  nothing.
* The game is waited for up to a minute, so starting this before MW2 is fine.
* If the game runs as administrator, run the EXE as administrator too.
* `iw4x.exe` is still a **32-bit** client, so this x64 DLL cannot load into it —
  the launcher detects that and says so instead of failing silently.
* If the DLL is already injected it says so. Re-injecting cannot re-run it, so
  either press **F6** twice (off, then on again) or restart the game.
* **`closeWithGame`** in `[general]` controls what the window does: `1` (default)
  closes it when the game exits, `0` leaves it open so values can be changed
  afterwards.
* The F6 toggle covers the counters too, so `[drawfps]` disappears along with the
  rest when you toggle the unlocker off. Set `enabled=0` there if you never want
  it.
* Run it from a shell and it also prints its progress to stdout; started from
  Explorer there is no console at all, because the window is the interface.

## The live channel

A slider that only takes effect on the next launch is a config editor, not a
control. The DLL reads its configuration once, at injection, so it needs to be
told about a new value while the game is running.

Windows already has a mechanism for that which needs no ports, pipes or agreed
file paths: a window. The DLL creates a hidden top-level window inside the game
process, the launcher finds it by class name, and the values travel as
`WM_COPYDATA`:

```
launcher                                   game process
  FindWindowW("MW2UnlockerLive")    --->   hidden window, created by the DLL
  SendMessageTimeout(WM_COPYDATA)   --->   WndProc -> features::ApplyLive(values)
```

Both binaries include `src/ipc.h`, which defines the window class, the message
and the value struct - so the two cannot drift apart - and the struct carries a
protocol version that each side checks before reading it.

`WM_COPYDATA` and not a message of our own, which matters more than it looks:
messages at or above `WM_USER` are delivered verbatim, so a pointer in `lparam`
would be a pointer into the *sender's* address space. Following it is an access
violation inside a window procedure, which the kernel escalates to
`STATUS_FATAL_USER_CALLBACK_EXCEPTION` and the game dies with it. That is exactly
what a first version of this did, once, in a live match. `WM_COPYDATA` is
marshalled by the kernel, so the struct and its payload are copied into the
receiving process first.

The hotkey rides in the same message even though it is not a cvar: the worker
thread polls it, and the window procedure runs on that thread, so a plain `int`
is enough to make a key change take effect without re-injecting.

Two details are what make it safe to drag a slider repeatedly:

* `Patcher::AddOrUpdate` replaces the bytes of an existing patch but keeps the
  **original** bytes captured the first time. Re-capturing on every update would
  make a later restore put back our own previous value instead of the game's.
* Each patch carries an `active` flag. Switching a feature off reverts its bytes
  and clears the flag, so the watchdog that re-applies values the engine resets
  (`cg_fov` on a respawn) does not immediately undo that switch.

`SendMessageTimeout` is used rather than `SendMessage`: a busy or hung game must
not freeze the launcher with it.

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
