# Feature report

Every cvar this project writes, most likely to work first. Written after the crash
on a map load, and the crash is the reason the order is what it is.

Binary: retail 64-bit `iw4mp.exe` (`D:\Steam\steamapps\common\Call of Duty Modern Warfare 2`),
4,901,944 bytes, image base `0x140000000`.

## How a claim here is established

Three things, none of which is enough on its own.

1. **Does the cvar exist?** The name string is looked for as a whole string, not as
   a fragment, so `cg_laserlight` is correctly reported absent even though
   `laserLight` exists.
2. **Does anything read it?** `tools/disasm.py --dvar <name>` walks name → registration
   → the static the registration caches the `dvar_t*` in → every reference to that
   static. The store is the registration, so anything past it is a reader. A cvar
   with zero readers is written and ignored — that is the whole of
   `docs/finding-signatures.md`'s dead-cvar table.
3. **Did the write land?** The unlocker logs the value it read back through the
   cached pointer after patching. `reads back +0x10 = ... (float 0.1)` means the
   engine's own copy of the value is now what we set — which separates "the
   unlocker failed" from "the game ignores it".

**Being read is not the same as being visible.** A cvar can be read by code that
never runs in the situation you are in: the fps counter is read in single player
only, and a HUD element that multiplayer never creates cannot show a change no
matter what its properties say. That distinction is most of the table below.

## Ranked

| # | Feature | cvar(s) | Read | Verdict |
|---|---|---|---|---|
| 1 | Frame rate cap | `com_maxfps` | 1 reader at `+0x1F3AC2` | Works. The project's original feature |
| 2 | Field of view | `cg_fov` | 2 readers at `+0x8124A`, `+0xB7DE9` | Works, plus the `+0x44` clamp |
| 3 | Mute the music | `snd_enableStream` | 3 readers | Works — you confirmed it in game |
| 4 | Fullbright world | `r_fullbright` | 6 readers | Read in six places; a switch that cannot be ignored |
| 5 | Hide the HUD | `cg_draw2D` | 3 readers | Read by the HUD master path |
| 6 | Hide the weapon model | `cg_drawGun` | 4 readers | Read four times |
| 7 | Disable the fog | `r_fog` | 1 reader | Read once, from the view setup |
| 8 | Viewmodel offsets | `cg_gun_x`, `cg_gun_y`, `cg_gun_z` | 1 reader each: `+0xC253A`, `+0xC2585`, `+0xC25CA` | Read once each where the viewmodel origin is built |
| 9 | Film tweak | `r_filmUseTweaks` + `r_filmTweakEnable` + six parameters | 1 reader each, all in one block at `+0x21287`…`+0x2131C` | Read. **Two gates**: with `r_filmUseTweaks 0` the renderer never copies the family |
| 10 | Objective pointer text size | `objectiveFontSize` | 1 reader at `+0xB0577` | **Written, and nothing happened.** The write is confirmed in the log (`reads back … float 0.1`); the objective pointer is a single-player element, so multiplayer never builds it |
| 11 | Health bar | `cg_drawHealth` | 2 readers at `+0xDC213`, `+0xDC583` | **Written, and nothing happened** — same reason, same log evidence (`reads back +0x10 = int 1`) |
| 12 | Laser beam | `laserRadius`, `laserLightRadius`, `laserRange`, `laserFlarePct` | 1 reader each | Applied, not yet observed. The beam belongs to the campaign's laser designator, so a multiplayer match may never draw one |
| 13 | Glow | `r_glow_allowed`, `r_glow`, `r_glowUseTweaks`, `r_glowTweakEnable`, `r_glowTweakRadius0`, `r_glowTweakBloomIntensity0` (+2) | 1 reader each | **Works.** Confirmed in game once the guard below was in: with the four gates open and the values turned up the bloom covers the screen and washes it out completely. Now a switch and four sliders in the window |
| 14 | Blur | `r_blur` | 1 reader | **Works.** "Dev tweak to blur the screen" at 0 with a minimum of 0, read once where the blur pass is set up. No gate, so the window writes it whenever it saves; its slider sits at the foot of the glow column |
| 14b | Black level | `r_blacklevel` | 4 references, 2 of them value reads | "Black level (negative brightens output)" at 0 between -0.99 and +0.99. **Written, and nothing moved.** Two of the four references are the engine's modified-flag sweep (`call sub_273AF0` = `mov byte ptr [rcx + 0xD], 0; ret`); the two real reads (`+0x30B14`, `+0xF1790`) are a compare-and-re-apply path, not the frame loop, so the value only lands at renderer set-up. No control — the section is file-only |
| 15 | Mouse sensitivity | `sensitivity` | 1 reader | Read every frame, and writing it was measured not to change the aim. Removed from the window for that reason |
| 16 | HUD safe area (adjusted) | `safeArea_adjusted_horizontal`, `safeArea_adjusted_vertical` | 4 and 5 references | **Works.** The pair the game's own Options > Safe Area menu writes, and what `getadjustedsafearea*` returns; registered at 1.0. One switch and two sliders, on the right of the viewmodel card |
| 16b | HUD safe area (base) | `safeArea_horizontal`, `safeArea_vertical` | 3 and 4 references | Read by code, and registered at 0.85 — but **writing it moved nothing on screen**, tested with sliders alongside the adjusted pair. Its controls were removed and the sections are file-only. Registered in the same block as the pair above (`sub_F2BD0`), read together with them in `sub_F2F10`, and read again in `sub_F2D10` where the rectangles are scaled by the screen dimensions |
| 17 | Compass size | `compassSize` | 10 references, read as a float from `+0x10` | Read by code. "Scale the compass", registered through the float helper at 1.0 with 0 as its minimum and `FLT_MAX` as its maximum, cached at `0x5A9C70`. A HUD size, so its slider sits under the safe area's — but it does **not** ride that switch: it is gateless like blur, so unticking the safe area leaves the compass alone instead of handing it back to the game's own value. The slider stops at 5.00 because the engine passes no usable ceiling |
| 18 | Crosshair | `cg_drawCrosshair` | 1 reference, at `+0xA9700` | Read by code, so it works. An int registered at 1 through the int helper (`mov dl, 1`, flags 4) — "Turn on weapon crosshair" — cached at `0x809990`. The read is `cmp byte ptr [rax + 0x10], 0` followed by a jump over a block of HUD code, so any non-zero value draws it and 0 does not. Now the fourth switch row of *Other Settings*, "Hide the crosshair" — inverted on purpose, so enabling it writes that 0 |
| 19 | Timescale | `timescale` | 1 reader, `movss` out of `+0x10` at `+0x1F427D` | Read by code. A float registered at 1.0 and cached at `0x1D26580`: the game's own clock. 1.0 is neutral, below it slows the game down and above it speeds everything up. **Testing setting**, in the window's *Server* card — behind that card's one switch, which goes into this section's `enabled` *and* `[phys_gravity]`'s; with the switch off the cvar is handed back to the game rather than held |
| 20 | Physics gravity | `phys_gravity` | read as a float out of `+0x10`, cached at `0x1B53BE0` | Read by code. "Physics gravity in units/sec^2.", registered through the float helper at 800 — the gravity on *objects* rather than the player — and 800 is neutral, so a negative number inverts it. **Testing setting**, in the *Server* card beside the timescale and sharing that card's one switch (both are restored when it is off); its slider runs over whole units from -2000 to 2000, starting at **-800** — the same magnitude with the sign flipped, the end worth trying |
| 21 | Lobby animation speed | `lobby_animationSpeed` | 2 readers, `+0x25CFD8` and `+0x25CFE8` | Read by code. Registered at rva `0x260467` through the helper that takes a min and a max (`sub_274D50`, the one the animation family uses) with the description "How long each frame of the animation should draw, in milliseconds", default 30 and `INT_MAX` as its maximum; cached at `0x666EEC0`. Both readers load it as an **int** out of `+0x10`: `mov ebx, [rax + 0x10]` then `imul ebx, [rax + 0x10]` with the tile count, and the animation frame is `elapsed / (tiles * speed)` — so it is a divisor as well as a rate, and **0 would divide by zero**. Multiplayer only: the name is not in `iw4sp.exe`. Nothing in the unlocker sets it |
| 22 | Snapshot rate | `snaps` | read *by name* at `+0x2285F9`; no cached readers | Registered at rva `0xFB681` through the same min/max helper, with "Snapshot rate" as the description string that sits immediately before the name, default 20 and 30 as its maximum (flags `0x201`, so archived). The pointer it returns is **thrown away** — there is no cache, so there can be no cached reader. The one read of the name is a lookup into a buffer at `+0x2285F9`, in the demo/network code, and what it finds is parsed as text (`+0x22863A`, default 1000 when it will not parse). Multiplayer only: the name is not in `iw4sp.exe` |
| — | — | `splitscreen` | **0 readers in either binary** | Registered in `iw4mp.exe` (rva `0x2927F3`, cached at `0x67B81B0`) and in `iw4sp.exe` (rva `0x28FA7C`, cached at `0x1BBAA70`), and read by nothing in either, so setting it does nothing. It is in the same registration block as the rest of the split-screen family, all of which are registered with no reader |
| — | — | `g_gravity` | **not in `iw4mp.exe` at all** | The name string is absent and a lookup finds no candidate for it, so in multiplayer there is nothing to set. It *is* in `iw4sp.exe` — registered at rva `0x75FC6`, cached at `0x3DD5E8`, read by code — so it works in single player only, like `cg_drawFPS`. The multiplayer binary's gravity is `phys_gravity` ("Physics gravity in units/sec^2.", read by code), with `phys_gravity_ragdoll`, `phys_gravityChangeWakeupRadius` and `glass_fall_gravity` beside it — those are for objects and glass, not for the player |
| — | — | `cg_drawFPS` | **0 readers in multiplayer** | Cannot work in MP. 6 readers in `iw4sp.exe`, so the counter is single-player only |
| — | — | `cg_drawFPSLabels`, `cg_drawViewpos`, `drawLagometer`, `lagometer` | 0 readers each | Registered, never read. Cannot work anywhere |
| — | — | `cl_motdString`, `motd` | 0 readers each | Registered, never read. `motd` is only ever *written*, by a startup path that also tests `PLATFORM_NOMOTD_MP` |
| — | — | `laserLightWithoutNightvision`, `laserDebug` | 0 readers each | Registered, never read |
| — | — | `cg_laserlight` | not in the file | The name is `laserLight`; the family is `laser*` |

## The crash, and what it turned out to be

The run's log (`logs/mw2_unlocker-game.txt`) shows all twelve enabled features
located, patched and read back:

```
[19:55:57] features: 12 enabled feature(s)
[19:56:05] features: 'r_glow' dvar located at 0x7FF7775859F0 (rva 0x4059F0)
[19:56:08] patcher: applied 'r_glowTweakRadius0+0x10' (4 bytes at 0x7FF77D846980)
[19:56:08] features: 'r_glowTweakRadius0' reads back +0x10 = int 1109393408 (float 40)
[19:56:08] patches applied successfully
```

Then the game died on the next map load, with nothing further in the log.

**Resolved.** The guard described below was added, the same twelve features were
applied again, and the game ran — and the glow was unmistakable, which settles
both halves of it: the crash was the clobber, and the chain works.

**`r_glow` is the anomaly.** Every other cvar located in the runtime dvar pool at
`rva 0x66Bxxxx`–`0x66Dxxxx`. `r_glow` located at `rva 0x4059F0`, and the bytes
dumped from there are not a `dvar_t`: at `+0x10`, where a dvar keeps its value,
this structure holds a pointer into the module (`0x7FF7774DEAB0`). We wrote the
integer 1 over that pointer. Anything that dereferences it afterwards finds `0x1`.

The static image says the same thing. `tools/disasm.py --dvar r_glow` now prints:

```
dvar: r_glow
  name string at rva 0x3C5BC0
  ! image pointer to the name at rva 0x4059F0 - a structure, not the dvar
  registration at rva 0x2F79E caches the dvar at rva 0x8CF7D78
  0x8CF7D78: 1 reader(s) of the cached pointer
```

That structure is a table — `{&name, 0, 0, {&string, index, flag}, …}` — and it
sits at a lower address than the real dvar, so a scan that takes the first pointer
to the name finds it. The real `r_glow` dvar is the one the registration caches at
`rva 0x8CF7D78`, in the same pool as everything else.

Of every cvar this project writes, **only `r_glow` has such a table**, which is why
the crash arrived with the glow block and not before.

### The fix

`LocateDvar` no longer accepts the first structure that points at the name. Each
candidate is checked before anything is written to it: for the integer and float
types, the value at `+0x10` may not be an address inside the module. A real value
is a small number whose upper half is zero, so the test cannot reject a genuine
dvar; and it skips the impostor and carries on scanning, so `r_glow` now finds the
real dvar instead of merely being avoided.

The check is in `src/features.cpp`, and the same trap is now reported by
`tools/disasm.py` as the `! image pointer` line above. The values in the glow and
laser blocks were also moderated for the retest (radius 5 → 12 rather than 40,
bloom 20 → 60 rather than 200), so that a second crash cannot be blamed on a
number being unreasonable.

### If it crashes again

The ordering to try, one step at a time, each a one-line `enabled=` change in
`unlocker.ini`:

1. the glow block off, laser on — isolates glow from laser;
2. the glow *gates* on with the values left at the game's defaults — isolates the
   gate path from the values;
3. one value at a time.

The log will say which. `features: '<name>' skipped the structure at 0x… - its
value slot holds a pointer into the module` is the guard firing; its absence means
the crash was something else.

## The logs

| file | what it is |
|---|---|
| `logs/mw2_unlocker-game.txt` | the live log from the game, `%LOCALAPPDATA%\MW2Unlocker\mw2_unlocker.log`, which is where the locate, patch and read-back lines above come from |
| `logs/mw2_unlocker-harness.txt` | the same log from the off-line harness run, where the host is `powershell.exe` — the "could not find the cvar" lines there are expected and mean the plumbing worked |
