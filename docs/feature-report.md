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
   `docs/finding-signatures.md`'s dead-cvar table. Two things have to be checked
   before that count is believed, and both are in the tables below:
   * **the store is the call's own.** A dvar registered twice has two stores to the
     same cache, so one reference counts as a reader when it is really the second
     registration — `com_statmon` is the case. The description and the name of a
     dvar are loaded in either order, so the nearest name above a store proves
     nothing; the proof is that the store writes `rax` straight after its own
     `call sub_2748xx`.
   * **a read of `+0xD` is not a read of the value.** The engine's modified-flag
     sweep tests the byte at `+0xD` and calls `sub_273AF0`
     (`mov byte ptr [rcx + 0xD], 0; ret`), which is how `r_blacklevel` and
     `r_debugShader` come to look read. A real read is of `+0x10` and out.
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
| 3 | Disable Music | `snd_enableStream` | 3 readers | Works — you confirmed it in game. Labelled in the window's rejoin colour: the game re-reads it when a match is set up |
| 4 | Fullbright | `r_fullbright` | 6 readers | Read in six places; a switch that cannot be ignored. Also labelled in the rejoin colour |
| 5 | Hide HUD | `cg_draw2D` | 3 readers | Read by the HUD master path |
| 6 | Hide Weapon model | `cg_drawGun` | 4 readers | Read four times |
| 7 | Disable Fog | `r_fog` | 1 reader | Read once, from the view setup |
| 8 | Viewmodel offsets | `cg_gun_x`, `cg_gun_y`, `cg_gun_z` | 1 reader each: `+0xC253A`, `+0xC2585`, `+0xC25CA` | Read once each where the viewmodel origin is built |
| 9 | Film tweak | `r_filmUseTweaks` + `r_filmTweakEnable` + six parameters | 1 reader each, all in one block at `+0x21287`…`+0x2131C` | Read. **Two gates**: with `r_filmUseTweaks 0` the renderer never copies the family |
| 10 | Objective pointer text size | `objectiveFontSize` | 1 reader at `+0xB0577` | **Written, and nothing happened.** The write is confirmed in the log (`reads back … float 0.1`); the objective pointer is a single-player element, so multiplayer never builds it |
| 11 | Health bar | `cg_drawHealth` | 2 readers at `+0xDC213`, `+0xDC583` | **Written, and nothing happened** — same reason, same log evidence (`reads back +0x10 = int 1`) |
| 12 | Laser beam | `laserRadius`, `laserLightRadius`, `laserRange`, `laserFlarePct` | 1 reader each | Applied, not yet observed. The beam belongs to the campaign's laser designator, so a multiplayer match may never draw one. The family's switch is `laserLight` — "Whether to draw the light emitted from a laser (not the laser itself)" — and it *is* read by code in both binaries (reader at rva `0xB2D84` in `iw4mp.exe`, `0xA5645` in `iw4sp.exe`) as a gate: `mov rax, [cache]; cmp byte ptr [rax + 0x10], 0; je <skip>`. The block it guards is the laser-light drawing: it reads `laserLightRadius` (cache `0x5AB398`, read at `0xB2DDF`), `laserLightEndOffset` (`0x5AB3A8`, at `0xB2DA9`) and `laserEndOffset` (`0x5AB3B0`, at `0xB2DD0`). One more condition sits beside the gate — `cmp dword ptr [rbp + 0x98], 1; jne <skip>` — so the light is drawn in that mode only. Its two siblings do nothing: `laserLightWithoutNightvision` (cache `0x5AB390`) and `laserDebug` (`0x5AB3C8`) are registered with **0 readers**. **Tested, and discarded:** a section writing `laserLight 1` was enabled for a match and changed nothing — which is what its own registration predicts, since the engine registers it at 1 already, so writing 1 has nothing to alter. Only closing the gate (0) could show a difference, and only where a laser is drawn |
| 13 | Glow | `r_glow_allowed`, `r_glow`, `r_glowUseTweaks`, `r_glowTweakEnable`, `r_glowTweakRadius0`, `r_glowTweakBloomIntensity0` (+2) | 1 reader each | **Works.** Confirmed in game once the guard below was in: with the four gates open and the values turned up the bloom covers the screen and washes it out completely. Now a switch and four sliders in the window |
| 14 | Blur | `r_blur` | 1 reader | **Works.** "Dev tweak to blur the screen" at 0 with a minimum of 0, read once where the blur pass is set up. No gate, so the window writes it whenever it saves; its slider is now in the Debug card, beside the near plane. **Its ceiling is 32.0**: the float helper takes the maximum in `xmm3`, and this registration's arrives in that register, loaded from rva `0x3CAA74` (`32.0f`) a dozen registrations earlier in the same block — the minimum is `xorps`ed to 0 and the default is 0. The reader at `0x637D5` does not clamp it either: it compares the value against zero (`comiss xmm0, xmm6; ja`) and then squares it into a length. The window's slider now runs the whole of that range, 0.00 to 32.00 |
| 14b | Black level | `r_blacklevel` | 4 references, 2 of them value reads | "Black level (negative brightens output)" at 0 between -0.99 and +0.99. **Written, and nothing moved.** Two of the four references are the engine's modified-flag sweep (`call sub_273AF0` = `mov byte ptr [rcx + 0xD], 0; ret`); the two real reads (`+0x30B14`, `+0xF1790`) are a compare-and-re-apply path, not the frame loop, so the value only lands at renderer set-up. No control — the section is file-only |
| 15 | Mouse sensitivity | `sensitivity` | 1 reader | Read every frame, and writing it was measured not to change the aim. Removed from the window for that reason |
| 16 | HUD safe area (adjusted) | `safeArea_adjusted_horizontal`, `safeArea_adjusted_vertical` | 4 and 5 references | **Works.** The pair the game's own Options > Safe Area menu writes, and what `getadjustedsafearea*` returns; registered at 1.0. One switch and two sliders, on the right of the viewmodel card. Its *Horiz. adj.* and *Vert. adj.* labels are drawn in the rejoin colour, like the music and fullbright switches: the game re-reads the safe area when a match is set up |
| 16b | HUD safe area (base) | `safeArea_horizontal`, `safeArea_vertical` | 3 and 4 references | Read by code, and registered at 0.85 — but **writing it moved nothing on screen**, tested with sliders alongside the adjusted pair. Its controls were removed and the sections are file-only. Registered in the same block as the pair above (`sub_F2BD0`), read together with them in `sub_F2F10`, and read again in `sub_F2D10` where the rectangles are scaled by the screen dimensions |
| 17 | Compass size | `compassSize` | 10 references, read as a float from `+0x10` | Read by code. "Scale the compass", registered through the float helper at 1.0 with 0 as its minimum and `FLT_MAX` as its maximum, cached at `0x5A9C70`. A HUD size, so its slider sits under the safe area's — but it does **not** ride that switch: it is gateless like blur, so unticking the safe area leaves the compass alone instead of handing it back to the game's own value. The slider stops at 5.00 because the engine passes no usable ceiling |
| 18 | Crosshair | `cg_drawCrosshair` | 1 reference, at `+0xA9700` | Read by code, so it works. An int registered at 1 through the int helper (`mov dl, 1`, flags 4) — "Turn on weapon crosshair" — cached at `0x809990`. The read is `cmp byte ptr [rax + 0x10], 0` followed by a jump over a block of HUD code, so any non-zero value draws it and 0 does not. The *Hide Crosshair* switch in *Other Settings* closes the card's middle column, inverted on purpose so enabling it writes that 0 |
| 19 | Timescale | `timescale` | 1 reader, `movss` out of `+0x10` at `+0x1F427D` | Read by code. A float registered at 1.0 and cached at `0x1D26580`: the game's own clock. 1.0 is neutral, below it slows the game down and above it speeds everything up. **Testing setting**, in the window's *Server* card — behind that card's one switch, which goes into this section's `enabled` *and* `[phys_gravity]`'s; with the switch off the cvar is handed back to the game rather than held. Its slider spans 0.50 to 5.00: the registration's minimum is 0.001 and the value is never clamped — the reader at `+0x1F427D` is a load, a multiply by `com_timescale` and a return — so the floor is measured rather than the engine's, because below about 0.5 the game's own time sync fights the slowdown and the picture rubberbands |
| 20 | Prop gravity | `phys_gravity` | read as a float out of `+0x10`, cached at `0x1B53BE0` | Read by code. "Physics gravity in units/sec^2.", registered through the float helper at 800 — the gravity on *objects* rather than the player — and 800 is neutral, so a negative number inverts it. **Testing setting**, in the *Server* card beside the timescale and sharing that card's one switch (both are restored when it is off); the row is labelled *Prop Gravity* because that is the distinction; its slider runs over whole units from -2000 to 2000, starting at **-800** — the same magnitude with the sign flipped, the end worth trying |
| 21 | Lobby animation speed | `lobby_animationSpeed` | 2 readers, `+0x25CFD8` and `+0x25CFE8` | Read by code. Registered at rva `0x260467` through the helper that takes a min and a max (`sub_274D50`, the one the animation family uses) with the description "How long each frame of the animation should draw, in milliseconds", default 30 and `INT_MAX` as its maximum; cached at `0x666EEC0`. Both readers load it as an **int** out of `+0x10`: `mov ebx, [rax + 0x10]` then `imul ebx, [rax + 0x10]` with the tile count, and the animation frame is `elapsed / (tiles * speed)` — so it is a divisor as well as a rate, and **0 would divide by zero**. Multiplayer only: the name is not in `iw4sp.exe`. Nothing in the unlocker sets it |
| 22 | Snapshot rate | `snaps` | read *by name* at `+0x2285F9`; no cached readers | Registered at rva `0xFB681` through the same min/max helper, with "Snapshot rate" as the description string that sits immediately before the name, default 20 and 30 as its maximum (flags `0x201`, so archived). The pointer it returns is **thrown away** — there is no cache, so there can be no cached reader. The one read of the name is a lookup into a buffer at `+0x2285F9`, in the demo/network code, and what it finds is parsed as text (`+0x22863A`, default 1000 when it will not parse). Multiplayer only: the name is not in `iw4sp.exe` |
| — | — | `splitscreen` | **0 readers in either binary** | Registered in `iw4mp.exe` (rva `0x2927F3`, cached at `0x67B81B0`) and in `iw4sp.exe` (rva `0x28FA7C`, cached at `0x1BBAA70`), and read by nothing in either, so setting it does nothing. It is in the same registration block as the rest of the split-screen family, all of which are registered with no reader |
| — | — | `g_gravity` | **not in `iw4mp.exe` at all** | The name string is absent and a lookup finds no candidate for it, so in multiplayer there is nothing to set. It *is* in `iw4sp.exe` — registered at rva `0x75FC6`, cached at `0x3DD5E8`, read by code — so it works in single player only, like `cg_drawFPS`. The multiplayer binary's gravity is `phys_gravity` ("Physics gravity in units/sec^2.", read by code), with `phys_gravity_ragdoll`, `phys_gravityChangeWakeupRadius` and `glass_fall_gravity` beside it — those are for objects and glass, not for the player |
| — | — | `cg_drawFPS` | **0 readers in multiplayer** | Cannot work in MP. 6 readers in `iw4sp.exe`, so the counter is single-player only |
| — | — | `cg_drawFPSLabels`, `cg_drawViewpos`, `drawLagometer`, `lagometer` | 0 readers each | Registered, never read. Cannot work anywhere |
| — | — | `cl_motdString`, `motd` | 0 readers each | Registered, never read. `motd` is only ever *written*, by a startup path that also tests `PLATFORM_NOMOTD_MP` |
| — | — | `laserLightWithoutNightvision`, `laserDebug` | 0 readers each | Registered, never read |
| — | — | `cg_laserlight` | not in the file | The name is `laserLight`; the family is `laser*` |
| — | — | `cg_ScoresPing_Interval`, `cg_ScoresPing_MaxBars` | 1 reader each | Multiplayer: registered at rva `0xE531D` and `0xE52BF`, cached at `0x80E608` and `0x80E600`, each read once (`0xE3646`, `0xE3630`) — the ping bar geometry and count the scoreboard draws. Read by code, so setting them has an effect. The rest of the family sits beside them — `cg_ScoresPing_HighColor`, `_MedColor`, `_LowColor`, `_BgColor` — not checked further |
| — | — | `cg_scoreboardPingText` | 2 readers | Multiplayer: registered at rva `0xE569B`, cached at `0x80E5C0`, read at `0xE388E` and `0xE4625`. Read by code |
| — | — | `g_speed` | **not in `iw4mp.exe` at all** | Not even a fragment of the name is in the multiplayer binary, so in a match there is nothing to set. It is in `iw4sp.exe` — registered at rva `0x172B8F`, cached at `0xD9C040`, read once at `0x15BF60` — so it works in single player only, the same shape as `g_gravity` |
| — | — | `jump_height` | registered but never read | `iw4sp.exe` only: the name at rva `0x31F7A0`, registered at `0x7083B`, cached at `0x3D9658`, **0 readers** — a write would land and be consulted by nothing. Absent from `iw4mp.exe` |
| — | — | `aim_autoaim_enabled` | registered but never read | `iw4sp.exe` only: the name at rva `0x31DFD0`, registered at `0x69E60`, cached at `0x3CF0F0`, **0 readers**. Absent from `iw4mp.exe`, whose nearest `aim_` name is `aim_target_sentient_radius` |
| — | — | `laserForceOn` | registered in SP, unreachable | "Force laser sights on in all possible places (for debug purposes)", `iw4sp.exe` only: registered at rva `0xA5B47` with flags 4, its pointer cached at `0x400490`. The cache store is hoisted past the *next* two registrations — `laserRange` and `laserRangePlayer`, which are registered immediately afterwards — so the store is 17 instructions later, at rva `0xA5B98`, and a first-match scan of the registration site reports "no cached pointer seen" rather than the cache. The one read of that cache is a getter at rva `0xA5B10` (`mov rax, [cache]; movzx eax, byte ptr [rax + 0x10]; ret`), and **nothing calls the getter**: no rel32 call to it anywhere in `.text`, and no pointer to it in the file either. So the value is never consulted. Absent from `iw4mp.exe` |
| — | — | `seta` | a command, and it is registered | `iw4mp.exe` registers it at rva `0x1FB8B0` with `set`, `toggle`, `togglep`, `reset` and `setfromdvar`, each as `lea rdx, [handler]; lea rcx, [name]; call sub_1E69F0`. `seta`'s handler is rva `0x1FBD10`, real code, so it is a working console command rather than only a config-file keyword |
| — | — | `map_restart` | **not a command in multiplayer** | The name exists exactly once in `iw4mp.exe`, and its single reference is `mov rcx, [nextmap]; lea rdx, "map_restart"; call sub_2765E0` — a dvar setter, not a command registrar (`sub_2765E0` tests the dvar's type at `[rcx + 0xC]` before setting). So the game itself writes the *string* "map_restart" into `nextmap`; typing `map_restart` in the console reaches no handler. In `iw4sp.exe` it *is* registered with a real handler (rva `0x22CB30`, at `0x22BF67`) |
| — | — | `fast_restart` | single player only | No such string anywhere in `iw4mp.exe`. In `iw4sp.exe` it is at rva `0x350B40`, registered at `0x22BF3A` with a real handler at rva `0x22C680` |
| — | — | `kick`, `clientkick` | not commands in `iw4mp.exe` | There is no standalone `kick` string: the 40 strings containing it are the `bg_viewKick*`/`bg_shock_viewKick*` cvars, `sv_kickBanTime`, UI text, and the two `scr_kick_*` names below. `clientkick` appears only inside two literals the client *builds and sends* — `clientkick %d PLATFORM_STEAM_CONNECT_FAIL` and `clientkick %d PLATFORM_STEAM_AUTH_DENIED` — so the text exists but the command is the server's, not this binary's. Neither name is in `iw4sp.exe` |
| — | — | `scr_kick_time`, `scr_kick_mintime` | **not cvars: nothing registers them, and nothing reads them** | Multiplayer only — 0 hits for `scr_kick` in `iw4sp.exe`. Both names are in the compiled name pool at rva `0x450F8C` and `0x450FA4`: a run of length-prefixed records (`01 00 00 <len + 1>` then the name) with no pointers to any of them, holding multiplayer stat names (`higherrankkills`, `lowerrankkills`, `laststandkills`, `hostmigration_enoughplayers`, `otherweaponkills`, `thumperkills`, `weapon_explosive`, `suicides`, `headshots`) and a class name (`CLASS_CUSTOM1`). There is no registrar call site for either name, so no `dvar_t` exists for them at start-up: a config line or a console `seta` on them does nothing, and there is no cached pointer for the unlocker's locator to follow. They are script-side names, resolved by index into that pool rather than by address, which is why the binary carries them at all. **Trap:** for a name inside this pool, "0 references to the name string" does **not** mean the name is unused — every name in the pool reports 0 references, the stat names included, because nothing addresses them by pointer. `suicides` and `headshots` are the tell: their names *also* exist at `0x378A30` and `0x378A10`, where descriptors point at them from `0x377A98` and `0x377A78` (32 bytes apart). The two `scr_kick_*` names have no such descriptor anywhere |

| — | — | `cg_scoreboardPingText`, `cg_scoreboardPingGraph` | 2 readers and 1 reader | Multiplayer: "Whether to show numeric ping value" and "Whether to show graphical ping", registered two apart in the scoreboard's own block through the int helper — the text with **0** as its default (`xor edx, edx` at rva `0xE56A2`, cached `0x80E5C0`, read at `0xE388E` and `0xE4625`, where the flag picks one of two layout tables), the graph with **1** (`mov dl, 1` at `0xE56C1`, cached `0x80E5F8`, read at `0xE3E54` as a gate: `mov rax, [cache]; cmp byte ptr [rax + 0x10], 0; je <skip>` around its drawing call). So a match shows the graph and no number by default, which is what the two defaults say. `cg_scoreboardPingWidth` and `cg_scoreboardPingHeight` — the bar geometry — are registered beside them, and both ping names are *also* in the compiled name pool, so the scripts and menus reference them by name as well. **Works** — confirmed in a match with a config section writing text 1, graph 0 — and is now the window's **Numeric Ping** switch, alone in the third column of *Other Settings*: one switch over the pair, because the senses are opposite. Written into both sections, off in the shipped config |

| — | — | `com_statmon` | 3 gate reads; a fourth reference is the second registration's store | Multiplayer: an **int**, registered in the render block at rva `0x2FE9A` through the int helper (`sub_274840`, default `dl = 0`, flags 0) with the description "Draw stats monitor", cached at `0x1D25EE0`. It is registered a *second* time in the developer block at `0x1F457D`, and that site's store (`0x1F45C1`) writes the same cache — so the address has two stores and a first-match scan calls one of them a reader. Read by code three times as a gate: `0xF490C` chooses between the default `0x300` and `call sub_206EF0` with a `"code_warning_snapshotents"` context, `3000` and `4`; `0x206EFF` gates the body of that same function, which returns early when the dvar is 0; `0x1F3F35` gates a smaller block. The name is **not** a command — those two registrations are its only references. Its registered default is 0, so it is off and 1 turns it on. **Tested, and discarded:** a section writing `com_statmon 1` was enabled for a match and no overlay appeared. That is not a write that failed to land — the value slot is a plain int at `+0x10` and all three readers test exactly that slot as a gate — so the code behind those gates is what multiplayer does not reach: the same shape as `laserLight` and `cg_drawFPS`. The section was removed from the config |
| — | — | `r_subwindow` | 1 reader, and it is a real value read | Multiplayer: a **vec4** — "subwindow to draw: left, right, top, bottom" — registered through the multi-component helper `sub_275530` at rva `0x2E339` with four components passed on the stack, cached at `0x8CF7A80`. Its one reader, `0x243C5`, loads the cache and copies all four floats out of `+0x10`, `+0x14`, `+0x18` and `+0x1C` into a struct at `[rdi + 0x130]`, then uses them as the subwindow rectangle. So it takes four numbers: `0 1 0 1` is left 0, right 1, top 0, bottom 1 — the whole window. Not registered in `iw4sp.exe`. **Tested, and discarded:** the four numbers were a row of boxes in the window — the *Server* card's last row, then *Other Settings*' — and a match showed exactly what the analysis predicted: they scale and shift the frame the renderer draws, and change nothing else. Nothing was wrong with them, which is the point: a frame you can shrink is a developer's view rather than something to play in, so the four boxes, the four config sections and the four protocol fields were all removed. The dvar, its reader and the rectangle are still described here |
| — | — | `camera_thirdPerson` | 4 gate reads | Multiplayer: registered at rva `0x8B235` with the description "Use third person view globally", in the same block as `bg_forceDualWield` and `camera_thirdPersonOffset`, cached at `0x58D790`. Read by code as a gate four times: `0x8A696` is a predicate that answers whether third person is forced (it returns false both when the dvar is 0 and when the entity's state carries the first-person flags), then `0xB9140`, `0xBDDE7` and `0x198D4C`, each `mov rax, [cache]; cmp byte ptr [rax + 0x10], 0; je/jne <branch>`. Works — and is now the window's **Third person view** switch in *Other Settings*, which writes 1 while it is ticked and hands the engine's own value back when it is not |
| — | — | `g_knockback` | 1 value read | Multiplayer: a **float**, registered through the float helper `sub_274C20` — not the int helper, despite the name — with the description "Maximum knockback" at rva `0x19DAD0`, cached at `0x19B64D0`. The reader at `0x1976C6` is `cvtsi2ss xmm0, edx; mulss xmm0, dword ptr [rax + 0x10]; divss xmm0, [0x3588D8]`, so the value is scaled into the knockback. It is registered in the *server's* block — `ui_maxclients`, `sv_maxclients`, `g_synchronousClients`, `g_password`, `g_banIPs`, then this, then `g_maxDroppedWeapons` — so it is the host's setting: as a client, writing it locally does not change the match. Write it as a float |
| — | — | `player_meleerange` | **wrong spelling** — the dvar is `player_meleeRange`, capital R | The all-lowercase name is in neither binary. The real name is in both: `iw4mp.exe` at rva `0x3581F8`, registered at `0x8CA34`, cached at `0x58D9C0`, read in five places (`0x94F32`, `0x9A6BE`, `0xAF653`, `0x134DC3`, `0x187C7E`); `iw4sp.exe` at rva `0x323A68`, registered at `0x75EF6`, cached at `0x3DD768`, five readers. Read by code in both, so the spelling — not the value — is what has to change |
| — | — | `r_showtris` | **not in either 64-bit binary** | No such string in `iw4mp.exe` or in `iw4sp.exe`, and no `showTris` command either. The only mention anywhere is a menu label in `iw4mp.exe` at rva `0x4242D3` — `               Toggle ShowTris`, the padding in front of it saying it is a UI string rather than a name anything registers — and a case-insensitive scan for `showtris` over the whole file finds nothing else. So a config line or a console command on it reaches nothing |
| — | — | `r_debugShader` | 2 real reads, plus 2 modified-flag references | Multiplayer: an **enum**, registered through `sub_274B20` at rva `0x2E165`, default the string `"none"`, flags 4, cached at `0x8C597F8`. Its list is at rva `0x577970`: `none`, `normal`, `basisTangent`, `basisBinormal`, `basisNormal`, then NULL — the *names* are its values, and the dvar's value at `+0x10` is the enum *index* of the one it holds: `0x1D107` reads it and indexes a 16-byte-per-entry table at `0x3B4340`, and `0x54C00` gates on it and sets a global to 2. The other two references (`0x1D083`, `0x1D0A6`) are the engine's modified-flag sweep — they test the byte at `+0xD` and call `sub_273AF0` (`mov byte ptr [rcx + 0xD], 0; ret`), so they never look at the value. It takes 0 to 3, the four modes it is used with — a *value*, not a switch, which is why it was given a box rather than a tick. **Tested, and discarded:** a box writing 0 to 3 stood in *Other Settings* for a build and the modes work exactly as the list says — they draw the shaders' basis vectors over the world — but that is a developer's view rather than a look, so the box, its config section and its protocol field were removed. The enum, the list and the index still describe it if it is ever wanted again |
| — | — | `r_znear` | 1 value read | Multiplayer: registered at rva `0x2E387`, cached at `0x8CF7970`; its one reader, `0x24440`, is `mov rax, [cache]; maxss xmm0, dword ptr [rax + 0x10]` — the near plane, clamped against whatever is already in `xmm0` and then used together with `r_znear_depthhack` (cached at `0x8CF7A18`). Also in `iw4sp.exe` (rva `0x37EDE0`, registered at `0x21B77`, cached at `0x4116048`, one reader at `0x7F10`). Works — and is now the **Z near** box, the second row of the **Debug** card, beside blur: gateless, covering 0.00 to 999.00 in hundredths and starting at the engine's own 4.00. The 100 it began with in *Other Settings* was the window's own ceiling rather than the engine's — there is nothing in the registration that clamps it — and a larger value cuts the world away, since anything nearer than it is simply not drawn |
| — | — | `bg_bobMax` | **not in either 64-bit binary** — the MW2 names are `bg_viewBobMax` and `bg_weaponBobMax` | `bg_bobMax` is the older CoD4 spelling. Multiplayer: `bg_viewBobMax` at rva `0x3568E0`, registered at `0x8B9D8`, cached at `0x58D8A0`, two readers (`0x9ECFF`, `0x9EE30`); `bg_weaponBobMax` at rva `0x356C48`, registered at `0x8BBC0`, cached at `0x58D8E0`, two readers (`0x9F173`, `0x9FB1F`). Both read by code, so those are the names to use |

**How the cache was pinned down for each of these.** The registrar blocks load a dvar's description and name in either order — the compiler hoists whichever it likes — so the *nearest* name above a cache store is not proof of which dvar the store belongs to. What is proof is the call: each store writes `rax` immediately after its own `call sub_2748xx`, and `rax` survives the intervening `lea`s. `com_statmon` was the case that needed this: its second registration's store sits before the next call, so a name-based reading would have attached it to `timescale`.

### A second batch, checked the same way

Names from a list of "make you host"/"see through walls" tweaks. Run against the
64-bit retail pair, plus the 32-bit `iw4x.exe` in the folder — which turned out to
matter, because a third of the list is not a name in either 64-bit binary at all.

| | | name | verdict | evidence |
|---|---|---|---|---|
| — | — | `party_connectTimeout` | **int, read as a value — the list's `party_connecttimout` is a typo** | "Connect timeout", registered at rva `0x10AC56` in the party block — `party_matchedPlayerCount`, `party_autoteams`, `party_maxTeamDiff`, `party_minLobbyTime`, then this — cached at `0xE58538`. Two value reads, both the same shape: `mov ecx, esi; sub ecx, [rdi + 0x1858]; cmp ecx, [rax + 0x10]; jge` at `0x10443B`, and `sub eax, esi; cmp eax, [rcx + 0x10]; jle` at `0x109C26`. So it is a threshold on a countdown in the matchmaking code, and lowering it makes the party give up or move on sooner. Nothing about it selects a host, and `party_hostmigration` does not exist to go with it. **Not in the window** |
| — | — | `party_hostmigration` | **not in either 64-bit binary** | Absent from `iw4mp.exe` and `iw4sp.exe`. The only `hostmigrat` strings in retail are `hostmigration_start` (twice, rva `0x384858` and `0x385748`) and the name-pool record `hostmigration_enoughplayers` (`0x450CD4`) — no dvar. The string *does* exist in `iw4x.exe`, which is 32-bit, so the 64-bit unlocker cannot load into it |
| — | — | `cg_drawThroughWalls` | **not in either 64-bit binary** | A scan for `throughwalls` (case-insensitive, over whole strings) finds nothing in `iw4mp.exe` or `iw4sp.exe`. It is an IW4x name: the only hit anywhere is `iw4x.exe`'s own string at rva `0x2F1058` |
| — | — | `r_znear_depthhack` | **float, read as a value** | "Viewmodel near clip plane", registered at rva `0x2E3C9` — in the same block as `r_znear` (`0x2E387`) and `r_zfar` (`0x2E3FF`), which is what its name says — cached at `0x8CF7A18`, two readers: `0x2444F` loads it, flips its sign (`xorps` with a mask) and stores the result as the viewmodel's near plane, and `0x62A84` feeds it to `maxss`, the same clamp `r_znear` gets. So it is the *viewmodel's* near plane, separate from the world's. Read by code; no control in the window |
| — | — | `r_zfar` | **float, read as a value** | "Change the distance at which culling fog reaches 100% opacity", with the registration note "Set to 0 to disable fog"; registered at `0x2E3FF`, cached at `0x8CF7AD0`, three readers (`0x2DC40`, `0x2DD80`, `0x52C86`). Two of them are getters that substitute a default when the value is zero (`xorps xmm0, xmm0; movss xmm1, [rax + 0x10]; ucomiss xmm1, xmm0; jp/jne`), which is the "0 disables it" path the note describes. Read by code |
| — | — | `profile_setViewSensitivity` | **a command, not a cvar** | The name's only reference in `iw4mp.exe` is `lea rcx, [rip + 0x2761e9]; call sub_1E69F0` at rva `0xF0FC0` — `sub_1E69F0` is the *command* registrar, the one `seta` goes through — with `lea rdx, [handler]` pointing at rva `0xF2100`. `iw4sp.exe` has the same pattern at `0xD8E7E`. The handler asks the arg vector for `argv[1]` (`mov rcx, [rcx + 8]`), calls rva `0x328358` on it — a thunk to `strtod`/`atof`: it sets `errno` (`mov dword ptr [rax], 0x16`, EINVAL) and returns 0.0 for a NULL pointer, which is the CRT's shape — and stores the parsed double (narrowed with `cvtsd2ss`) at **rva `0x8197C4`**, with `comiss` against `0.01f` (`0x3c23d70a` at rva `0x3CA728`) and a second store of `0.01f` to the same address when the argument is below it. So `profile_setViewSensitivity 1.2` writes 1.2; `0` or a negative writes 0.01. Its neighbours are `profile_toggleInvertedPitch` and `profile_setButtonsConfig`, and the profile key is the string `viewSensitivity` (rva `0x366F68`), which sits in a run of option keys — `percentcompleteso`, `autoAim`, `invertedPitch`, `viewSensitivity`, `gpadButtonsConfig`, `gpadSticksConfig` — none of which any code points at, because they are looked up by index. The only other writer of `0x8197C4` found in the file is rva `0xF0A45`, inside the controls-defaults reset: it writes `1.0f` there (`0x3f800000` at rva `0x3CA7F4`), `0.8f` to the two stick sensitivities beside it, a flag byte, and loads the strings `buttons_default` and `thumbstick_default`. So the address is the profile's view-sensitivity field, defaulting to 1.0, and this is the console's way to set it. There is **no dvar**, so the unlocker's locator has nothing to hold and the config's section format cannot express it — the same shape as `seta` itself. Whether the game applies the field immediately or at profile load is not visible in the image: no code touches the address apart from those two sites, so any other reader reaches it through a base pointer, and the one measurement this project has on the subject is a warning rather than a proof — writing the `sensitivity` **dvar**, which *is* read every frame, was measured not to change the aim in game (`sensitivity` row above) |
| — | — | `scr_game_forceuav` | **a name-pool record, not a cvar** | The name occurs once, at rva `0x40B98C`, and nothing in the file points at it — 0 references. The bytes say what it is: a run of `01 00 00 <len>` records, `01 00 00 0F` then `scr_game_forceuav`, sitting between `scr_game_perks` and `scr_game_hardpoints`. A script-side name resolved by index, so no `dvar_t` exists and a console `seta` on it reaches nothing |
| — | — | `compass_show_enemies` | **not in any of the three binaries** | Zero hits for `show_enemies` anywhere. The name it is reaching for is the next row |
| — | — | `g_compassShowEnemies` | **a name-pool record, not a cvar** | Same shape as `scr_game_forceuav`: one occurrence at rva `0x4405C4`, 0 references, and the byte dump shows the record header `01 00 00 15` immediately before it, inside a run that also holds `cg_drawSpectatorMessages`. So it is a script/stat name rather than a registered dvar — which is why guides present it as something a *script* reads |
| — | — | `ui_debugMode` | **int, read as a gate — works** | "Draw ui debug info on the screen." in `iw4mp.exe`, "Shows ui debug information on screen." in `iw4sp.exe` — the two binaries carry different descriptions for the same dvar. Registered at rva `0x2605AC` (between `ui_drawCrosshair` and the hardcore-HUD one) cached at `0x65FEE68`, five readers and every one of them a gate: `mov rax, [cache]; cmp byte ptr [rax + 0x10], 0; je` at `0x2510BE`, `0x254DD9`, `0x2589D6`, `cmp byte ptr [rax + 0x10], r14b` at `0x255300`, and `cmp byte ptr [rcx + 0x10], 0; sete dl` at `0x2540D2`. `iw4sp.exe` is the same: registered at `0x24DA93`, cached at `0x1A3A920`, five gate readers. So writing 1 turns the overlay on; there is no value to tune, and no control in the window |
| — | — | `scr_player_maxhealth` | **a name-pool record, not a cvar** | One occurrence at rva `0x40BC14` with 0 references, and the same `01 00 00 15` record header in front of it. `iw4sp.exe` has a *different* real name, `g_player_maxhealth` (rva `0x33BD40`), and `iw4mp.exe` has neither registered. A script/server-side setting: as a client there is nothing local to write |
| — | — | `compassRadarUpdateTime` | **float, read as a value in multiplayer only** | "Time between radar updates for the normal radar mode", registered at rva `0xA68C8` with `compassFastRadarUpdateTime` (`0xA68FE`) and `compassRadarLineThickness` (`0xA6934`) right after it — three consecutive caches at `0x5A9CB0`, `0x5A9CB8`, `0x5A9CC0`. Its reader at `0xCE7E9` is one half of a two-way choice: the code loads this cache, tests something, and either keeps it or swaps in `compassFastRadarUpdateTime` at `0xCE804`, then reads whichever it kept with `movss xmm0, [rax + 0x10]` and scales it. In `iw4sp.exe` it is registered (rva `0x91AD8`, cache `0x3FDDF0`) and read by **nothing** — single player has no radar to update |
| — | — | `compassFastRadarUpdateTime` | **float, read as a value in multiplayer only** | "Time between radar updates for the fast radar mode", registered at `0xA68FE`, cache `0x5A9CB8`, read at `0xCE804` as the other branch of the pair above. `iw4sp.exe`: registered at `0x91B0A`, **0 readers** |
| — | — | `compassRadarLineThickness` | **float, read as a value in multiplayer only** | "Thickness, relative to the compass size, of the radar texture", registered at `0xA6934`, cache `0x5A9CC0`, four readers and all four scale something by it: `mulss xmm3, [rax + 0x10]` at `0xCCCD5`, `movss xmm8, [rax + 0x10]` at `0xCE44D`, `mulss xmm2, [rax + 0x10]` at `0xCE627`, `mulss xmm2, [rax + 0x10]` at `0xD0059`. `iw4sp.exe`: registered at `0x91B40`, **0 readers**. Zero is a legal value and draws nothing, so it is a way to take the radar lines off the compass rather than a way to see through anything |
| — | — | `setplayerdata` | **not in either 64-bit binary** | Absent from `iw4mp.exe` and `iw4sp.exe`. It exists as a string in `iw4x.exe` (rva `0x2FD2CC`) only, which is the 32-bit client this unlocker cannot load into |

**Two things this batch settled beyond the names themselves.**

* `iw4x.exe` is a **32-bit** binary — PE32, `i386`, image base `0x400000`, 3,932,160 bytes — and
  so is `iw4mpold.exe` (4,704,752 bytes), the older multiplayer client. The
  64-bit pair is `iw4mp.exe` (6,254,136 bytes) and `iw4sp.exe` (4,480,056 bytes).
  This is why a name that is only in `iw4x.exe` can never be a feature here: the
  DLL is 64-bit and the launcher says so rather than failing to inject
  (`launcher/main.cpp` tries `iw4x.exe` last for exactly this reason).
* The disassembly tool builds a 64-bit reader, so pointing it at a 32-bit file
  makes every pointer search fail and reports "0 references" for names that
  certainly have them — `com_maxfps` in `iw4x.exe` reports 0 as well. Treat a
  0-reference result as evidence only after the machine type has been read: the
  COFF `Machine` field at `pe + 4` (0x8664 x64, 0x14C i386) and the optional
  header `Magic` at `pe + 24` (0x20B PE32+, 0x10B PE32), which is also what
  decides where `ImageBase` sits.

### The view-sensitivity experiment, and what it turned up about `sensitivity`

`profile_setViewSensitivity` was then wired up as a `float_ptr` feature, so the
same field could be written without a console: the signature is the command
handler's store plus its floor, `dispOffset=4`, `instrLen=8`, and the value 1.5.
It resolved correctly in the running game and **the aim did not change**:

```
[01:13:52] features: 14 enabled feature(s)
[01:14:03] features: 'viewSensitivity' signature matched at 0x7FF752BE2139
[01:14:03] features: 'viewSensitivity' resolved RIP-relative global -> 0x7FF7533097C4
[01:14:03] patcher: applied 'viewSensitivity' (4 bytes at 0x7FF7533097C4)
```

Subtract the module base (`0x7FF752BE2139 - 0xF2139 = 0x7FF752AF0000`) and the two
addresses are exactly the rvas found statically: the match at `0xF2139` and the
field at `0x8197C4`. So the write landed and the client did not act on it. That is
the answer to the open question the section existed to settle: the field is not
consumed while a match is running — it is written by the Controls menu and read
when the profile is dealt with, not per frame. **Tested, and discarded**, with the
section left in the config at `enabled=0` because the recipe (the AOB, the two
rvas) is worth keeping and `[sensitivity]` set the precedent for keeping a
file-only section that was tried.

The same session turned up something about a feature that was *not* being tested,
and it changes an older conclusion. The log shows `[sensitivity]` never writing at
all:

```
[01:13:52] features: 'sensitivity' disabled, skipping
...
[01:30:06] features: 'sensitivity' cvar name candidate at 0x7FF752E58638 (rva 0x368638) reads as 'sensitivity'
[01:30:06] features: 'sensitivity' nothing points at that string, so it is a label, not a cvar name
... (all four candidates rejected the same way)
[01:30:07] features: 'sensitivity' none of the 4 name candidate(s) for 'sensitivity' is a dvar
[01:30:07] features: live update for 'sensitivity' skipped - the cvar was not located
```

The dvar certainly exists — registered at rva `0xFB36A` through the *float* helper
`sub_274C20` with the description "Mouse sensitivity", cached at `0xE51B20` and
read once at `0xF6D5B`, default `5.0` and maximum `100.0` (`0x3CAA14` and
`0x3CAA98`) — and the registration site is `lea rcx, [name]; ... call sub_274C20;
mov [0xE51B20], rax`. So what failed is the *locator*, not the dvar: the only
image pointer to the image copy of the name is the structure the tool also
flags (`! image pointer to the name at rva 0x40A930 - a structure, not the dvar`),
and the two other candidates are the tails of `viewSensitivity` and
`profile_setViewSensitivity`. Which means the row below this file's oldest claim —
"writing `sensitivity` was measured not to change the aim" — is not what the
current build does: **nothing was written**. Either the measurement was taken
against a different `iw4mp.exe` (the recorded size for that file does not match
this install; see `finding-signatures.md`), or the locator has never been able to
find this one since. Before `[sensitivity]` is dismissed again, the locator needs
to be able to reach it — the candidate it must accept is the structure at
`0x40A930`, whose shape is the `r_glow` trap in reverse: there the trap was a
lower-address structure and the real dvar was found anyway; here it is the only
pointer there is.

### A third batch: the console-command list

`Commands.txt` (2,005 lines, a community list) is three things at once, and only one
of them is useful: engine cvars, GSC script source, and names that do not exist in
this build. Every cvar-looking token that our own notes did not already cover — 298
of them — was run through the same chain, and the ones that came back *registered and
read* are these:

| Name | Shape | Evidence |
|---|---|---|
| `party_minplayers` | int, six value/gate reads | Registered at `0x10A88E`, cached at `0xE58488`: `mov edi, [rcx + 0x10]; sub edi, eax; cmp edi, 1`, `cmp r14d, [rax + 0x10]; jge` — lobby thresholds. Party/host-side, so a client's copy is only the client's view of them. |
| `party_maxplayers` | int, **thirty** readers | Registered at `0x10A8BE`, cached at `0xE58618`; every site reads `[rax + 0x10]` into a count or a loop bound. The lobby's capacity, and the most-watched number in the party block. |
| `party_gameStartTimerLength` | int, four readers | Registered at `0x10A8F2`, cached at `0xE58490` — the countdown before a match starts. |
| `sv_maxclients` | int, registered twice, 7 + 15 readers | `0x19DA3B` caches `0x19B64C0` (the server block) and `0x2289D2`/`0x2290AA` cache `0x653BD58`; the other six sites a first-match scan calls registrations report "no cached pointer" because the store is hoisted past the next call — the `laserForceOn` shape, not a mystery. Server-side. |
| `g_hardcore` | registered **twice**, **read by nothing** | `0x19D9EC` and `0x228F70` both cache `0x19B3B18`, and the only two references to that cache are the two stores. Dead — the `com_statmon` shape; the hardcore rules live in the gametype scripts. |
| `cg_footsteps` | int **gate**, four readers | Registered at `0xD8816`, cached at `0x809BB0`; every reader is `mov rax, [cache]; cmp byte ptr [rax + 0x10], 0; je`. Client-side and live: 0 is "stop playing footstep sounds". |
| `player_breath_hold_time` | float, three **value** reads | Registered at `0x8BD2F`, cached at `0x58D918`: `movss xmm0, [rax + 0x10]; mulss xmm0, xmm1; cvttss2si edi, xmm0` — the held breath the scope gets, scaled on the way to an int. Client-side and live. |
| `developer_script` | int gate, six readers | Registered at `0x1F4530`, cached at `0x1D26090`: three `cmp dword ptr [rcx + 0x10], 0; je` gates around a script-diagnostic call, and three byte reads feeding it. Client-side. |

`r_filmTweakInvert` is in the window: the *Invert the grade* switch on the film tweak
card's fourth row — the left column's last, level with the glow's *Desat* slider.
"Tweak dev var; enable inverted video" — an int registered at 0,
cached at `0x8CF7A20` with flags `0x40`, read once. It has no gate cvar of its own,
so the switch *is* the flag: ticking it writes the value and unticking hands the
engine's own value back. It ships off, and it shows only while the grade itself is
on. Two more from this list were given switches for a build and then taken back out:
`cg_footsteps` (*Mute Footsteps*, inverted because the cvar is registered at 1) and
`ui_debugMode` (*UI debug*). Both were tried in a match and both worked, and neither
earned a permanent control, so their sections are file-only now. All of this is
described in `docs/how-it-works.md` and exercised by `tools/smokelive.ps1`
(protocol 23).

**And the rest of that list, by category**, because a list that is wrong half the
time is worth knowing about:

* **Not in `iw4mp.exe` at all** — `badhost_endGameIfISuck`, `scr_thirdperson` (the
  real name is `camera_thirdPerson`), `aim_lockon_strength`, `aim_lockon_deflection`,
  `aim_input_graph_enabled`, `cg_debug_overlay_viewport`, `scr_dm_timelimit`, and
  `scoresping_interval` (the real name is `cg_ScoresPing_Interval` — and for this one
  the case matters, because the name is looked up as a string). `player_sustainammo`
  was in this list and should not have been: the binary registers
  `player_sustainAmmo`, with a capital A, which an exact-string search misses. It is
  registered — and read by nothing, so it is dead anyway. See the fourth batch.
* **A name-pool record, so not a cvar**: `scr_player_forcerespawn`, `scr_teambalance`,
  `g_compassshowenemies` (the same record the `g_compassShowEnemies` row above is
  about — the file's spelling is the script's).
* **Registered but read by nothing**: `scr_game_allowkillcam` (0 readers),
  `ragdoll_fps` (0 readers). Writing them does nothing.
* **Single-player only**, per the rows above: `g_gravity`, `g_speed`, `jump_height` —
  the file quotes multiplayer values for all three.
* **Wrong spelling**: `party_connecttimeout` — the dvar is `party_connectTimeout`.
* **Everything else is GSC**: `setClientDvar`, `setModel`, `setPlayerData`, `statSet`,
  `playFx`, `loadfx`, `notifyOnPlayerCommand`, `bind`, the weapon/attachment/camo
  names, the killstreak tables and the `map mp_*` lines are *script source*. They are
  not console lines: they only do anything in a modded or offline context where those
  scripts run, and `setClientDvar` on a name the engine never registered does nothing
  at all — which is exactly what the `g_compassshowenemies` line does.

### A fourth batch: six more names

Checked with `--dvar`, and with `--candidates` where a name looked absent, because
`--candidates` tries every case variant a lookup would and an exact search of the file
does not.

| Name | Verdict | Evidence |
|---|---|---|
| `cg_debugposition` | **absent from both binaries** | 0 occurrences, and `--candidates` finds 0 as well, in `iw4mp.exe` and in `iw4sp.exe`. Not a cvar here at all — a CoD4/GSC name. |
| `cg_drawSnapshot` | **registered, read by nothing** | "Draw debugging information for snapshots", an int registered at `0xD7891` through the int helper (default 0, flags 1), cached at `0x809988`. The only reference to that cache is the store, so writing it does nothing. Its neighbour in the same block, `cg_drawCrosshair` at `0x809990`, is the live one. |
| `compassEnemyFootstepEnabled` | **absent in multiplayer; dead in single player** | `iw4mp.exe`: 0 occurrences, candidates included. `iw4sp.exe`: registered at `0x92016`, cached at `0x3FDE40`, 0 readers — so it does nothing there either. |
| `perk_footstepVolumePlayer` | **float, read by code — client-side and live** | Registered through the float helper at `0x8DFB0`, cached at `0x58DAE0`, read at `0xD71BB`. Default **0.25**, minimum 0, maximum `FLT_MAX` (the constant at `0x3CAB38`), flags 4. The volume of *your own* footsteps, as a multiplier. |
| `perk_footstepVolumeAlly` | **float, read by code** | Registered at `0x8DFF3`, cached at `0x58DAE8`, read at `0xD71F3`. Default 0.25, minimum 0, maximum `FLT_MAX`. A teammate's footsteps. |
| `perk_footstepVolumeEnemy` | **float, read by code — the interesting one** | Registered at `0x8E007`, cached at `0x58DAF0`, read at `0xD7204`. Default **4.0**, minimum 0, maximum `FLT_MAX`. An enemy's footsteps. |
| `player_debugHealth` | **dead in multiplayer, live in single player** | `iw4mp.exe`: "Turn on debugging info for player health", an int registered at `0x8BF65` (default 0, flags `0x8C`), cached at `0x58D970`, 0 readers. `iw4sp.exe`: registered at `0x753F4`, cached at `0x3DD710`, and **read at `0x15B459`**, so it does something offline. |

The three footstep volumes are one family, registered together in `sub_8D9B0` and
read by one function, the one at `0xD7190` — and that function picks *between* them,
which is the whole point of it:

```
D71AC  movss xmm6, [0x3CA7F4]        ; 1.0 - the fallback
D71B4  je 0xD7210                    ; taken unless a global flag (bit 0x20000000) is set
D71B6  test r9b, r9b                 ; is the source the local player?
D71B9  je 0xD71C4
D71BB  mov rax, [0x58DAE0]           ; perk_footstepVolumePlayer
D71C4  ...team / perk index compare...
D71F3  mov rax, [0x58DAE8]           ; perk_footstepVolumeAlly
D7204  mov rax, [0x58DAF0]           ; perk_footstepVolumeEnemy
D720B  movss xmm6, [rax + 0x10]      ; the multiplier that is returned
```

So the game quietly plays an enemy's footsteps **four times** louder than the
engine's neutral 1.0, and your own and your team's at **a quarter**, and these three
numbers are that mix. Because the minimum is 0 and the maximum is `FLT_MAX`, the
useful direction is either way: `perk_footstepVolumeEnemy 0` mutes enemies entirely,
and a value above 4.0 pushes them further up, while lowering the Player/Ally pair is
the "quiet footsteps" half. All three are client-side and live — read per footstep
event, not at level load — so a live write is enough. None is in the window; this trio
is what a footstep-volume control would be built on if one is ever wanted.

### A fifth batch: five more names

| Name | Verdict | Evidence |
|---|---|---|
| `bg_forcedualwield` | **wrong case — the dvar is `bg_forceDualWield`** | The string in the file is `bg_forceDualWield` (rva `0x355A88`), and `--candidates` on the lowercase spelling says "different case, starts a string". Registered at `0x8B215` through the int helper — default **0**, flags `0xC`, "Force akimbo for all possible weapons" — cached at `0x589C18`, **3 readers** (`0x99D8A`, `0x18837E`, `0x18849A`). Read by code, but the readers sit in weapon set-up, so it is probably only honoured when a weapon is given rather than mid-fire. Its neighbour in the same block is `camera_thirdPerson`. |
| `ThermalVisionFOFOverlayOn` | **not a cvar** | `iw4mp.exe`: 0 occurrences, candidates included. `iw4sp.exe`: the string `thermalvisionfofoverlayon` (all lowercase) at rva `0x343C78`, and the only thing pointing at it is an *image pointer* at `0x342F28`, which the tool flags as "a structure, not the dvar" — the `r_glow` shape. 0 references to the name string, so nothing registers it: it is a field name inside a vision/overlay structure, and there is nothing for a console command to set. |
| `perk_blastShield "65"` | **a script string-table record, not a cvar** | The name is at rva `0x40BA04` with **0 references**, and the bytes around it are the giveaway: `01 00 00 11 70 65 72 6B 5F 62 6C 61 73 74 53 68 69 65 6C 64 00` — the `01 00 00 <len>` name-pool header, length `0x11` = 17 = the name plus its NUL — then `01 00 00 03` + `"65"`, then `01 00 00 03` + `"40"`, then `01 00 00 19` + `perk_armorPiercingDamage`. The `"65"` and the `"40"` are *this file's data*, and they are the very numbers the command list quotes. A perk parameter table read by scripts, so setting it as a dvar does nothing. |
| `perk_armorPiercingDamage "40"` | **the same record table** | The `"40"` sitting immediately before `perk_armorPiercingDamage` in that dump is its value; same `01 00 00 <len>` shape, 0 references. Script-side. |
| `player_sprintSpeedScale 1` | **float, read by code — live and client-side** | "The scale applied to the player speed when sprinting", registered through the float helper at `0x8C17B`, cached at `0x58D728`, flags `0xCC`. Default **1.5** (`0x3CA870`), minimum **0**, maximum **2.0** (`0x3CA980`), and **2 readers** — `0x8F7F5` and `0x90678` — each of them `mov rax, [cache]; mulss reg, dword ptr [rax + 0x10]`, so it *scales* the sprint speed rather than replacing it. |

`player_sprintSpeedScale` is the one worth remembering here. The command sets it to
**1**, which is *slower* than the shipped 1.5 — two thirds of the game's sprint speed —
and the useful end is the other one: **2.0** is the registration's own maximum, so it
is the fastest the engine will accept, and because both readers only multiply a value
on its way into the movement code, it applies as a live write rather than at a
restart. The two constants it is built from, the 1.5 default and the 2.0 cap, are
shared with other registrations in the same block, which is why the disassembly has to
be followed back to `0x8BB7A` and `0x8BE66` to read them off.

Both of the live names from this batch were given controls, and only one of them
survived. `bg_forceDualWield` was a **Force dual wield** switch for one build: tried
in a match, it moved nothing — the dvar *is* read, but not anywhere that changes a
match — so the control has been removed and the section is file-only now.
`player_sprintSpeedScale` worked, and it is the **Sprint speed** slider (id 1108)
with its number box (1109), at the foot of the *Debug* card; it covers the
registration's whole 0.00 to 2.00 and starts at the engine's own 1.50, so a slider
nobody has touched writes what the game already had. Protocol 25 carries that one
field. The other three in the batch remain what they were found to be — two script
records and one name that is not a cvar at all.

### A sixth batch: four pause flags

| Name | Verdict | Evidence |
|---|---|---|
| `cg_drawpaused "1"` | **real, and already 1** | "Draw paused screen", an int registered through the plain int helper with default **1** (rva `0xD8E5D`, cache `0x809CD0`). Read in exactly two places, both in the pause-overlay code, and only reached *after* `cl_paused` is non-zero — `cmp byte ptr [rax + 0x10], 0; jne <skip the draw>` at `0xA9455` and `0xA96F6`. So `cg_drawpaused 1` is a no-op: it is what the engine ships. `cg_drawpaused 0` is the interesting one — pause with no screen. Also in `iw4sp.exe` (registered at `0xA823F`, 12 readers). |
| `cl_paused "1"` | **real, and the actual client pause flag — but the name resolves to two cvar objects** | "Pause the game", an int registered through the min/max int helper at `0xD8E36` with default 0 and flags `0x2000`, cached at `0x809CC8` — **six references, every one a read** (`cmp dword ptr [rax + 0x10], 0`), at `0xA9441`, `0xA96E2`, `0xE3F16`, `0xEC36F`, `0x20F561`, `0x210E22`. Nothing in the client writes it, so a local 1 holds, and the pause paths do take the paused branch (`0xA9430` reaches the pause-screen draw, `0xE3F16` early-outs a frame-update path). **The catch**: the name string is referenced ten times in `iw4mp.exe`, and a *second* cvar object is cached at `0x1D265C8` (registered at `0x1F46AD`, described "Pause the client") with **16 readers** of its own. A locator that finds the dvar by name patches one object; the readers of the other keep seeing the old value — a strong candidate for "the command did nothing". `iw4sp.exe` is worse: fourteen sites and two cached objects (21 and 34 readers). |
| `mp_paused "1"` | **real, multiplayer only, and the narrowest** | "If true ignore server time advancing.  Handy for taking hi-resolution screenshots without the world moving" — registered at `0xBC7BE`, cached at `0x5AF6C8`, with exactly **one** reader (`0xB81CB`). This is the screenshot-freeze flag, and of the four it is the one most likely to visibly stop the world. Not in `iw4sp.exe` at all. |
| `sv_paused "1"` | **real, multiplayer only, and the engine clears it** | "Pause the server", registered at `0x1F467D` (the block that also registers "Pause the client"), cached at `0x1D265D0`, four references (`0xF50BE`, `0xFAD0F`, `0x22B241`, `0x22B2F3`). One of the readers' neighbours **writes the dvar**: at `0x22B241` the cvar object itself is handed to `sub_276240` with `edx = 0`, which is the setter — so the game puts it back to 0 on its own and a local 1 is undone. It is the server's flag; on a client it can only put you out of step. Not in `iw4sp.exe`. |

The pair worth remembering is the split in `cl_paused`: **registered twice, under the
same name, with different reader sets** — the client copy at `0x809CC8` and a second at
`0x1D265C8`. Anything that patches a dvar by name patches one of the two, which is
exactly the shape that makes a pause command look like it did nothing. For a file-only
section, the two worth trying are `cg_drawpaused 0` (pause with no screen) and
`mp_paused 1` (stop the world for a screenshot); `cl_paused` and `sv_paused` are state
the engine owns. `mp_paused` has since been given a control: it is the **MP paused**
switch beside the sprint scale on the *Debug* card's last row, so it can be tried
without editing the file.

### A survey: what else is in the string table

Rather than testing names one at a time, the whole file was swept with `--grep` for the
words a renderer or a client tweak tends to use — `vsync`, `decal`, `specular`,
`shadow`, `dlight`, `distortion`, `feather`, `water`, `sprint`, `breath`, `kick`,
`spread`, `thermal`, `killcam`, `pause`, `give` — and every name that looked like a
cvar was then put through `--dvar`. The ones that came back **registered and read**
are below. All are client-side unless the row says otherwise, and they are *candidates*,
not features: none has been tried in a match yet.

| Name | Shape | What it is, and the evidence |
|---|---|---|
| `r_vsync` | int, 1 reader | a video toggle — registered at `0x2ECF7`, cached at `0x8CF7C90`. The first thing to check for a frame-rate unlocker. |
| `r_distortion` | int, 1 reader | "Enable distortion" — registered at `0x2F73E`, cached at `0x8CF78C0`. 0 turns off the heat-haze/refraction post effect. |
| `r_drawDecals` | int, **7 readers** | "Enable world decal rendering" — registered at `0x2EDCF`, cached at `0x8CF7968`. Off removes bullet decals and blood splats: a frame-rate win and a clarity one at once. |
| `r_specular` | int, 5 readers | "Allows shaders to use phong specular lighting" — registered at `0x2E5FC`, cached at `0x8CF7C88`. |
| `r_specularMap` | int, 2 readers | "Replace all specular maps with pure black (off) or pure white (super shiny)" — registered at `0x2E6E8`, cached at `0x8CF7880`. |
| `r_spotLightShadows` | int, **9 readers** | "Enable shadows for spot lights" — registered at `0x2EEED`, cached at `0x8CF7A28`. Its siblings are `r_spotLightSModelShadows` and `r_spotLightEntityShadows`. |
| `r_dlightLimit` | int, 2 readers | the cap on dynamic lights — registered at `0x2EE04`, cached at `0x8CF7908`. Lower it for frames, raise it for looks. |
| `r_zFeather` | int | "Enable z feathering (fixes particles clipping into geometry)" — the name at `0x3C5418`, beside `r_outdoorFeather`, "Outdoor z-feathering value". |
| `player_sprintUnlimited` | int, **4 readers** | "Whether players can sprint forever or not" — registered at `0x8C19B`, cached at `0x58D978`, in the same block as the sprint scale the window already carries. |
| `player_sprintTime` | float, 1 reader | "The base length of time a player can sprint" — registered at `0x8C1B5`, cached at `0x58D980`; the registration's own range is 4.0 to 12.8. |
| `player_spectateSpeedScale` | float, 2 readers | the spectator camera's movement speed — registered at `0x8C0F5`, cached at `0x58D718`. |
| `bg_aimSpreadMoveSpeedThreshold` | float, 1 reader | "When player is moving faster than this speed, the aim spread will increase" — registered at `0x8BCBA`, cached at `0x58D908`. |
| `bg_viewKickScale` | float | "The scale to apply to the damage done to calculate damage view kick" — registered at `0x8B0DF`; its store is one of the hoisted ones, so no cache could be tied to it (the `laserForceOn` shape). Its siblings `bg_viewKickMax`, `bg_viewKickMin` and `bg_viewKickRandom` are in the same block. |
| `thermalBlurFactorScope`, `thermalBlurFactorNoScope` | floats, 1 reader | the blur drawn through, and outside, a thermal scope — registered at `0xBD1F9`, cached at `0x5AF800`. |
| `cg_waterSheeting*` | a family of ten | `cg_waterSheeting`, `..._enable`, `_distortionScaleFactor`, `_magnitude`, `_radius`, `_contrast`, `_brightness`, `_desaturation`, `_darkTint`, `_lightTint`, `_fadeDuration` — the film tweak's shape, for the water-on-the-lens effect. |
| `cg_heliKillCam*`, `cg_airstrikeKillCam*` | a family | the FOV and near/far blur of the killstreak cameras (`...KillCamFov`, `...KillCamNearBlur`, `...KillCamFarBlur`, `...FarBlurStart`, `...FarBlurDist`) — 72 strings in the file between them. |
| `cg_flashbangNameFadeIn` / `Out` | ints | "Time in milliseconds to fade in friendly names after a flashbang". |
| `g_giveAll` | int, **50 readers** | "Give all weapons" — registered at `0x19E78A`, cached at `0x19B6638`. Fifty readers: it is the flag the weapon-give code tests everywhere. It sits in the server block beside `sv_maxclients`, so as a client there is nothing local for it to change. |

**Traps found on the way**, recorded so the sweep is not repeated:

* `recoilScale` (`0x383058`) is **not a cvar** — an image pointer sits at `0x381710`,
  the "structure, not the dvar" shape. It is a *weapon-file field*, in the same region
  as `weapCommon.aimSpreadScale` and `weapCommon.spreadOverride`. Recoil lives in the
  weapon data, not in a dvar, so it is not reachable the way this tool writes things.
* `r_glowBloomIntensity1` and `r_glowBloomDesaturation` (`0x35EAF8`, `0x35EAC8`) are
  the same trap inside the glow family: an image pointer and 0 references. The
  *registered* glow tweaks are the `r_glowTweak*` ones the window already uses.
* `cg_drawFPSLabels` (`0x360070`, "Draw FPS Info Labels") is registered at `0xD77F0`
  and **read by nothing** — the `cg_drawSnapshot` shape from the fourth batch.
* There is no `motionBlur`, `depthOfField`, `postAA`, `sharpening`, `corona`,
  `lensflare`, `r_sky`, `antialias`, `swapInterval`, `r_marks`, `noclip` or `freecam`
  string in this binary at all. Those are later engines' names, or they were never in
  the game.

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
