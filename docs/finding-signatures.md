# Finding the FPS / FOV signatures with x64dbg

Goal: produce the `signature`, `matchOffset` and `patch` values that
[`config/unlocker.ini`](../config/unlocker.ini) needs. You do this **once** per
game build.

> **Safety first:** do this on the **single-player** executable (`iw4sp.exe`),
> offline. Do **not** attach a debugger to `iw4mp.exe` / a live VAC-secured
> multiplayer session — that can get the account banned.

> **Use the 64-bit debugger.** Launch `x64\x64dbg.exe`, **not** `x32\x32dbg.exe`.
> The game is 64-bit now; a 32-bit debugger cannot attach.

---

## Step 0 — Prepare the debugger and game

1. Install x64dbg (the download ships both `x32` and `x64` folders). Use
   `x64\x64dbg.exe`.
2. Start MW2, load into a level so the engine is fully up (many globals and
   code paths only exist once a map is running).
3. In x64dbg: **File → Attach** (`Alt+A`) → select `iw4sp.exe` → **Attach**.
   The game freezes.
4. Press **F9** (Run) to let the game carry on rendering. You can keep playing
   while attached; x64dbg only stops when a breakpoint hits.
5. Open **View → Modules** and confirm `iw4sp.exe` is listed. Note that you can
   refer to addresses as `iw4sp.exe+OFFSET` anywhere x64dbg asks for an address.

---

## Step 1 — Decide what the FPS cap actually is

Open the in-game FPS counter if your build has one (dev console), so you can see
the cap (e.g. it sits at 91, 60, 85, 125 …) and confirm later that you broke it.

An FPS cap is stored/used as a **float**, either:

* a **writable global** in `.data` — then you can just overwrite it with
  `type=float` and never touch code, **or**
* a **constant/immediate** baked into code or `.rdata` — then you must patch the
  code that clamps against it (`type=bytes`).

Step 4 tells you which one you have. First find the float.

### Float → bytes cheat sheet

x64dbg searches byte sequences, so convert the float to little-endian IEEE-754
bytes:

| FPS value | Float hex    | Bytes to search |
|----------:|--------------|-----------------|
| 30.0      | `0x41F00000` | `00 00 F0 41`   |
| 60.0      | `0x42700000` | `00 00 70 42`   |
| 85.0      | `0x42AA0000` | `00 00 AA 42`   |
| 91.0      | `0x42B60000` | `00 00 B6 42`   |
| 100.0     | `0x42C80000` | `00 00 C8 42`   |
| 125.0     | `0x42FA0000` | `00 00 FA 42`   |
| 144.0     | `0x43100000` | `00 00 10 43`   |
| 200.0     | `0x43480000` | `00 00 48 43`   |
| 250.0     | `0x437A0000` | `00 00 7A 43`   |
| 500.0     | `0x43FA0000` | `00 00 FA 43`   |
| 1000.0    | `0x447A0000` | `00 00 7A 44`   |

MW2's classic **multiplayer** cap was **91.0** (`1000 / 11`, tied to the network
snapshot rate). Single-player values differ, so try several.

### Measured: what this specific build actually contains

I scanned the shipped `iw4sp.exe` and cross-checked the live process, so you can
skip the guesswork:

| Probe | Result |
|-------|--------|
| `91.0` float (`00 00 B6 42`) | **0 occurrences in the whole binary** |
| `1/91`, `1/60` floats | **0 occurrences** |
| `85.0` float | exactly **1** occurrence, at `iw4sp.exe+0x324310` |
| `100.0` / `200.0` / `250.0` floats | `+0x3436D6` / `+0x324320` / `+0x324328` |
| `com_maxfps` string | **present**, at `iw4sp.exe+0x34B2D0` |
| `cg_fov` string | **present**, at `iw4sp.exe+0x327BB4` |
| `cg_fovScale` / `cg_fovMin` | `+0x33F300` / `+0x33F3B8` |

**Conclusion: this build has no hard-coded 91 FPS cap.** The frame limit is
driven by the **`com_maxfps` cvar**, not by a magic float constant — which is
exactly why searching for `91.0` finds nothing. `iw4sp.exe` is also not packed
(normal `.text` / `.rdata` / `.data` sections), so all of the addresses above are
meaningful and stable.

Therefore the real targets are:

* **FPS** → the code that clamps against the `com_maxfps` dvar,
* **FOV** → the code that clamps `cg_fov` (MW2 traditionally clamps it to roughly
  65–80),

and both are reached the same way — start at the cvar's **name string** and
follow the code that references it.

> **Reality check before you invest effort:** the classic 91 FPS cap is a
> *multiplayer* concept (tied to the network snapshot rate). If single-player
> already runs above 400 FPS, there may be nothing to unlock for FPS at all, and
> the FOV side is the one that actually needs patching.

---

## Step 2b — The cvar route (works even when no float exists)

This is the reliable method for a cvar-driven value, and it needs no float
hunting at all:

1. **Ctrl+G** → type `iw4sp.exe+34B2D0` → Enter. You should land on the literal
   string `com_maxfps` (for FOV, use `iw4sp.exe+327BB4` and `cg_fov`).
2. In the **Disassembly**, right-click → **Search for → String references** (or
   with the string selected in the Dump, look for the "find references to this
   address" entry). Confirm the target and run it.
3. x64dbg lists every instruction that references that string. Those are the
   **dvar registration sites** — you want something shaped like
   `Dvar_RegisterFloat(&dvar, "com_maxfps", ...)`.
4. Go to that call and **F2** on the instruction *after* it. When it hits, the
   return value in **RAX** is the `dvar_t*`. That pointer is what the frame
   limiter later reads.
5. Put a **hardware breakpoint on the dvar's value field** inside that struct
   (`dvar_t` = name ptr, description ptr, flags, then the value). Do it by
   following the pointer in the Dump when the breakpoint from step 4 hits.
6. Whatever code reads that field every frame is your limiter. Patch its clamp:
   `type=bytes` with `patch=90 90` on the conditional jump, or overwrite a
   writable float with `type=float`.

---

## Step 2 — Locate the float in memory (in x64dbg)

1. In the **CPU** tab, right-click anywhere in the **Disassembly** pane →
   **Search for** → **Current Module**.
2. Type one candidate byte pattern, e.g. `00 00 B6 42`, and confirm.
3. Matches show up in the **References** tab. Double-click each hit to follow it
   in the **Dump**.
4. Repeat for the other candidate values until one sits in an interesting place
   (next to engine code, near a `com_`/`cg_` style string, or referenced by
   code that runs every frame).

> If your x64dbg build exposes an integer/float value search, you can use that
> instead of the byte search — same result.
>
> Cheat Engine (64-bit) is a fine alternative for this specific step: scan
> "Float / Exact value" for `91`, etc., then note the address. You can even
> right-click the address → *Find out what accesses this address* to jump
> straight to Step 3 and get an `iw4sp.exe+OFFSET` you can open in x64dbg with
> `Ctrl+G`.

---

## Step 3 — Find the code that reads the float

With the result selected in the **Dump**:

* Right-click → **Breakpoint → Hardware → Access** (some builds label it
  *Hardware, Access* / *Set hardware access breakpoint*).

If your build lacks that menu, use Cheat Engine's *Find out what accesses this
address*, note the reported `iw4sp.exe+OFFSET`, then in x64dbg press **Ctrl+G**
and type `iw4sp.exe+OFFSET` (or the raw RVA) to land on the instruction.

Then:

1. **F9** (Run). The breakpoint trips as soon as the game reads the value.
2. Read the surrounding disassembly. A frame limiter typically looks like:

   ```asm
   movss   xmm0, dword ptr ds:[rip+XXXXXXXX]   ; load cap / frame time
   comiss  xmm0, xmm1
   jae     iw4sp.exe+YYYYYY                    ; <-- branch that skips the frame
   ...
   ```

   The **conditional jump** (`jae` / `jb` / `jle` …) is your patch target.
3. Record its address. Press **Ctrl+P** later to see the *Patches* window and
   confirm what you changed.

---

## Step 4 — Writability check (decides `type=float` vs `type=bytes`)

Open **View → Memory Map**, find the section containing the address you found,
and read its **Protect** column:

* **`RW`** (e.g. `.data`) → it is a variable. You can skip code patching entirely
  and use `type=float` to write a bigger value. Easiest possible unlock.
* **`R` / `R-X` / `R--`** (e.g. `.rdata`, `.text`) → it is a constant or an
  immediate. You must patch the code (`type=bytes`).

---

## Step 5 — Verify the patch live (before touching the .ini)

Keep it in x64dbg first — no recompiling, no injection:

1. Go to the conditional jump from Step 3.
2. Select it and right-click → **Binary → Fill with NOPs** (or press **Space**
   to assemble `nop` over it). Equivalently, in the **Dump** select the bytes and
   press **Ctrl+E** to edit them.
3. **F9** to run and watch the in-game FPS counter.
   * Cap gone / FPS rises → correct site.
   * Nothing changes or it crashes → wrong site, undo with **Ctrl+P → restore**
     (or Ctrl+Z) and look again one instruction earlier/later.

Once the live edit works, you know the exact patch bytes.

---

## Step 6 — Extract the signature

You want a stable byte string around the patch site.

1. In the **Disassembly**, click the **first** instruction you want, then
   **Shift+click** the last (~8–16 instructions: enough to be unique, short
   enough to survive refactors).
2. Read the **byte column** on the left of each instruction, in order, and write
   those bytes into `signature = ...`.

**What to wildcard (`??`)**

| Operand | Wildcard? | Why |
|---------|-----------|-----|
| `[rip+XXXXXXXX]` displacement | **Usually no** | With ASLR the instruction *and* its target move together, so the 32-bit displacement stays constant within a build. |
| An absolute 64-bit address of a heap allocation | **Yes** | Heap addresses change every run. |
| Immediates that are runtime pointers / IDs | **Yes** | Same reason. |
| Opcodes and branch targets *within* the module | **No** | Stable relative encoding. |

This is the key x64 win over the old 32-bit tools: RIP-relative code is far more
signature-stable than absolute-addressed x86 code.

**Keep the signature unique.** If the unlocker logs the wrong hit, lengthen the
signature. You can sanity-check occurrences in x64dbg by re-running the byte
search from Step 2 with your finished pattern.

---

## Step 7 — Choose the patch bytes

| Intent | `type` | `patch` |
|--------|--------|---------|
| Neutralise a 2-byte conditional jump | `bytes` | `90 90` |
| Invert a condition (`74`↔`75`, `7E`↔`7F` …) | `bytes` | the single new opcode |
| Force unconditional jump | `bytes` | `EB` (+ pad `90`) |
| Overwrite a writable float global (Step 4 = `RW`) | `float` | *n/a* — set `value=` |
| Write a float through a `[rip+disp32]` global | `float_ptr` | *n/a* — set `value=`, `dispOffset=`, `instrLen=` |
| Overwrite a float immediate in code | `bytes` | the 4 little-endian float bytes |

For `float_ptr`: in `movss xmm0, [rip+disp32]` the encoding is
`F3 0F 10 05 xx xx xx xx`, so `dispOffset=4` and `instrLen=8`.

---

## Step 8 — Write it into `unlocker.ini`

```ini
[feature:fps]
enabled=1
type=bytes
signature=48 8B 05 ?? ?? ?? ?? 48 85 C0 74
matchOffset=0
patch=90 90
```

`matchOffset` is added to the **start of the match**. If the byte you want to
change is the 12th byte of your signature, use `matchOffset=11`.

---

## Step 9 — Test with the unlocker

1. **File → Detach** in x64dbg (or close it) so it is not holding breakpoints,
   and restore any live edits — you want a clean game process.
2. Get back in-game.
3. Inject:

   ```
   .\build\Release\injector.exe iw4sp.exe "D:\Games\projects\fps-unlocker\build\Release\mw2_unlocker.dll"
   ```

4. After `delayMs`, open `mw2_unlocker.log` next to the DLL. You want:

   ```
   features: 'fps' signature matched at 0x...
   patcher: applied 'fps' (2 bytes at 0x...)
   patches applied successfully
   ```

5. Press the `toggleKey` (default **F6**) to flip the patches on/off and watch
   the FPS counter.

If the log says *signature not found*, go back to Step 6 and extend the pattern
(you probably wildcarded something that was actually stable, or the pattern is
too short/ambiguous).

---

## Troubleshooting: "the game didn't freeze when I attached"

Attaching normally suspends the process. If the game keeps running, work through
this in order.

### 1. Force a pause — but read 1a first, F12 can kill this game

Press **F12** (Pause) in x64dbg.

* **Game freezes →** you are attached correctly and you are done. F12 *is* the
  fix; there is nothing to change in the settings. Continue hunting signatures.
* **Game still runs →** you are **not** attached to the game. Go to step 3.
* **Game crashes →** that is a known problem on these binaries. See 1a.

### 1a. F12 crashes the game with STATUS_BREAKPOINT

Measured from the Windows Application event log after an F12 pause:

```
Faulting application: iw4mp.exe      (separately, iw4sp.exe)
Faulting module:      ntdll.dll      (iw4sp died inside iw4sp.exe instead)
Exception code:       0x80000003     = STATUS_BREAKPOINT
Fault offset:         ntdll.dll+0x160a04
```

`0x80000003` is `STATUS_BREAKPOINT`: the process executed an `int3` /
`DbgBreakPoint` that nothing handled, so it terminated. F12 works by injecting a
thread that executes `DbgBreakPoint`, so *forcing* a pause is exactly what trips
this. Both binaries have crashed this way.

**Rules that avoid it:**

* **Do not press F12 on these binaries.** Set a breakpoint at a specific address
  *first*, then let the game reach it naturally.
* **Prefer hardware breakpoints** (Disassembly/Dump → **Breakpoint → Hardware**).
  They do not modify code, so there is no `int3` to leave behind.
* **Clear every breakpoint before detaching or closing x64dbg** — Breakpoints tab
  → delete all, and check **Ctrl+P** (Patches). A stray `int3` left in memory
  after the debugger leaves produces exactly this `0x80000003` crash.
* If the game still dies without F12, suspect **anti-debug**. x64dbg's ScyllaHide
  plugin is the usual countermeasure — or skip the debugger entirely (below).

> **Prefer not to debug this game at all.** Our own DLL locates code by AOB
> scanning and patches bytes through `VirtualProtect`: no `int3`, no thread
> suspension, nothing that looks like a debugger. Use x64dbg only to *discover*
> addresses; do the actual patching with the DLL.

> **Multiplayer + VAC:** attaching any debugger to a VAC-secured `iw4mp.exe`
> session is risky on its own, independent of this crash. Do not do it while
> connected to a secured match.

### 2. There is no "Attach breakpoint" setting

For the record, since this trips people up: x64dbg's **Preferences → Events**
tab has **no "Attach breakpoint" checkbox**. Verified against a screenshot of
that exact dialog. The events it exposes are:

* System Breakpoint, Entry Breakpoint, Exit Breakpoint, Debug Strings
* user TLS Callbacks, user DLL Entry, user DLL Load, user DLL Unload
* Thread Entry, Thread Create, Thread Exit, SetThreadName
* System TLS Callbacks, System DLL Entry, System DLL Load, System DLL Unload

Note the legend at the bottom of that dialog: **`* Requires debuggee restart`**.
Anything marked with `*` only takes effect when you **launch** the debuggee — it
does nothing for an attach to an already-running process. So none of these
checkboxes can make an attach stop. **F12** is the tool for that.

If F12 works, you are fine. If it does not, the cause is not a setting — it is
a wrong process or a failed attach. See steps 3 and 4.

### 1b. "I am attached, but I cannot find iw4sp.exe in Modules"

The **title bar is the truth**. A successful attach looks like:

```
iw4sp.exe - PID: 25732 - Module: ntdll.dll - Thread: 7336 - x64dbg
```

If the first field names the game, you are attached and the game *is* frozen.

The **Modules** tab lists every loaded module — 100+ of them — so `iw4sp.exe` is
easy to miss. Fastest route into the game's code:

* **Ctrl+G** → type `iw4sp.exe` → Enter.

That drops you straight into the game module and the title bar then reads
`Module: iw4sp.exe`. Alternatively, in the Modules tab right-click the column
header and sort **by name**: `iw4sp.exe` sits alphabetically between
`inputhost.dll` and `KERNEL32.DLL`. Sorting **by address** puts the main
executable at the top instead, since the EXE usually loads at the lowest address.

Right after attaching you are parked at the system breakpoint inside
`ntdll.dll`, which is why the CPU view shows no game code at all. Press **F9**
to run, or use Ctrl+G as above.

### 1c. Overlays and mods that can interfere

If the process has `gameoverlayrenderer64.dll` (Steam overlay) or
`DiscordHook64.dll` loaded, those hook Direct3D inside the game. They are
normally harmless, but if you see odd crashes or misbehaving breakpoints,
disable the Steam in-game overlay and quit Discord.

If `iw4x.dll` is loaded, you are debugging the **IW4x** client rather than the
plain game: the code you want may live in *that* module instead of `iw4sp.exe`,
so pass its name via `gameExe=` in `unlocker.ini`. When an `iw4x.pdb` sits next
to `iw4x.dll` (44 MB of symbols), load it in x64dbg and you get real function
names — far better than hunting raw signatures.

### 3. Confirm you attached the right process

This is the most common real cause. Checks:

1. Look at the x64dbg **title bar** — it shows the path of the debuggee. It must
   be the **game**, not Steam.
2. **View → Modules** — the game executable must be listed. Typical names:
   * `iw4sp.exe` — single-player (the one you want),
   * `iw4mp.exe` — multiplayer (do **not** use, VAC).
3. If you attached `steam.exe`, `GameOverlayUI.exe`, a launcher, or a
   `steamwebhelper` process, the game is unaffected — that is exactly the
   "nothing froze" symptom.
4. In the attach dialog, sort by name and pick the process whose path is inside
   the MW2 install folder. If you cannot see it, the game is not running yet.

### 4. Privilege / bitness mismatches

* Attached silently doing nothing can mean the game runs **elevated** (Steam as
  administrator) while x64dbg does not. Close both and restart **x64dbg as
  administrator**, then attach again.
* Confirm you are using `x64\x64dbg.exe`. Attaching `x32\x32dbg.exe` to a 64-bit
  process fails outright — and a stale `x32` debugger pointed at the wrong PID is
  an easy mistake.
* Watch the **Log** tab: a successful attach logs an "attached to process" style
  message. If it logs an error, that is your answer.

### 5. Alternative: launch instead of attach

If attaching keeps misbehaving, start the game from the debugger instead:

**File → Open** → select the game's `.exe` (or use **File → Attach** after
starting it from Steam normally). Launching under the debugger breaks at the
system/entry breakpoint, which you can then clear with **F9**.

> Keep in mind: while the game is broken (frozen), it cannot respond to input.
> That is expected. Press **F9** whenever you want to keep playing, and it will
> break again at your next breakpoint.

---

## Localized UI — what changes, and what never does

Your screenshot showed the **Preferences** dialog rendering in **English** on a
Russian system, so this x64dbg build is not fully translated. Some menus may
still differ from this guide's wording. Two things never change:

* **Hotkeys** — `F12` pause, `F9` run, `F2` breakpoint, `F4` run to cursor,
  `F7`/`F8` step into/over, `Ctrl+G` go to, `Ctrl+E` edit bytes, `Ctrl+P`
  patches window, `Alt+A` attach.
* **Raw data** — the hex byte column in the disassembly and the memory
  protection codes (`RW`, `R-X`) in the Memory Map. Never translated, and
  exactly what Steps 4 and 6 depend on.

There is **no "attach breakpoint" setting** in `Preferences → Events`, in any
language, so do not go hunting for one. Use **F12**.

If you want the UI fully in English, look for a `Language` key in `x64dbg.ini`
(typically under `[Gui]` or `[General]`) or for a `translations` folder next to
`x64dbg.exe`.

### Full English → Russian reference for this guide

| English | Russian (approximate) |
|---------|-----------------------|
| File / Edit / View / Debug | Файл / Правка / Вид / Отладка |
| Trace / Plugins / Favorites / Help | Трассировка / Плагины / Избранное / Справка |
| CPU | ЦПУ / Процессор |
| Log | Журнал / Протокол |
| Breakpoints | Точки останова |
| Memory Map | Карта памяти |
| Call Stack | Стек вызовов |
| Notes | Заметки |
| Symbols | Символы |
| Modules | Модули |
| Threads | Потоки |
| References | Ссылки |
| Dump | Дамп |
| Search for | Поиск |
| Current Module / Current Region / All Modules | Текущий модуль / Текущая область / Все модули |
| Go to | Перейти |
| Follow in Dump / Follow in Disassembler | Показать в дампе / Показать в дизассемблере |
| Breakpoint | Точка останова |
| Hardware Breakpoint → Access/Write/Read | Аппаратная точка останова → Доступ/Запись/Чтение |
| Binary → Fill with NOPs | Двоичные данные → Заполнить NOP-ами |
| Copy | Копировать |
| Comment / Label | Комментарий / Метка |
| Find references | Найти ссылки |
| Patches | Патчи / Исправления |
| Undo / Restore | Отменить / Восстановить |
| Protect (Memory Map column) | Защита |
| Settings / Preferences | Параметры / Настройки |
| Events | События |
| Exceptions / Engine / GUI | Исключения / Движок / Интерфейс |

> These are community translations and wording changes between releases. Two
> things are **always** identical whatever the language: the **hotkeys**, and
> the **hex bytes / memory protection codes** (`RW`, `R-X`) you read out of the
> views. Rely on those when a label is unclear.

---

## Quick x64dbg shortcut reference

| Key | Action |
|-----|--------|
| `F9` | Run / resume |
| `F7` / `F8` | Step into / step over |
| `F2` | Toggle software breakpoint |
| `F4` | Run to cursor |
| `Ctrl+G` | Go to address — accepts `iw4sp.exe+1A2B3C` |
| `Space` | Assemble / edit the instruction at the cursor |
| `Ctrl+E` | Edit bytes in the Dump |
| `Ctrl+P` | Patches window (see and revert live patches) |
| `Alt+A` | Attach |

> Menu labels vary slightly between x64dbg releases (e.g. *Hardware, Access*
> vs *Set hardware access breakpoint*). The workflow is unchanged.

---

## FOV notes

FOV is usually a float in degrees (65, 70, 80, 90…), often as
`fov` / `cg_fov`-style globals. Search those float bytes (e.g. `90.0` =
`0x42B40000` = `00 00 B4 42`), then:

* if the found address is in a **writable** section → `type=float`, `value=90.0`;
* if it is read through `movss xmm0, [rip+disp32]` → `type=float_ptr`;
* if it is an immediate in code → `type=bytes` with the 4 float bytes.

Note the FOV cap: MW2 clamps FOV (commonly 65–80). If writing the value has no
effect, there is a clamp — find it the same way as Step 3 and NOP it.

---

## Multiplayer: `iw4mp.exe` (verified addresses)

The multiplayer clamp is **not** in `iw4sp.exe`. Use `iw4mp.exe`.

### Bitness of every executable in the install (measured)

| File | Image | Notes |
|------|-------|-------|
| `iw4sp.exe` | **x64** | single-player, 4,480,056 bytes |
| `iw4mp.exe` | **x64** | multiplayer, 4,901,944 bytes |
| `iw4mpold.exe` | x86 | old 32-bit MP |
| `iw4x.exe` | **x86** | IW4x client — still 32-bit |
| `iw4xxx.exe` | **x86** | IW4x variant |
| `iw4sp_codmod.exe` | x86 | |
| `zonebuilder.exe`, `CoDCleaner.exe`, launcher stubs | x86 | |

So official SP **and** MP both need a 64-bit debugger, while the IW4x client is
still 32-bit (`x32dbg`, and this project's x64 DLL cannot inject into it).

### Addresses found in `iw4mp.exe`

| Item | RVA |
|------|-----|
| `com_maxfps` string | `iw4mp.exe+0x38C3B8` |
| `cg_fov` strings | `+0x370E44`, `+0x370E78`, `+0x370EB0` |
| `cl_maxpackets` | `+0x3795E0` |
| `snaps` (network rate) | `+0x37115F`, `+0x3784E1`, `+0x3784F6`, `+0x37851D`, `+0x379968` |
| float `85.0` | **one occurrence**, `+0x3698C4` |
| float `250.0` | **one occurrence**, `+0x3698D8` (20 bytes after the 85.0) |
| float `125.0` | `+0x3ABCCC` |
| float `500.0` | `+0x3E4B14` |
| float `1000.0` | `+0xA7B4F`, `+0xA7B60`, `+0xA7CD9`, `+0xA7CEA`, `+0x3E4B24` |
| float `0.001` (1/1000) | `+0x1DEF43`, `+0x1DEF4B`, `+0x2B3A81`, `+0x2B3C9F`, `+0x38BB1C` |
| float `91.0` | **absent — no hard-coded 91 cap exists** |

### Two concrete leads

**Lead 1 — the frame limiter math (best starting point).**
`iw4mp.exe` is 8 sections with `.text` spanning RVA `0x1000`–`0x361E00`. The four
`1000.0` hits at `+0xA7B4F`, `+0xA7B60`, `+0xA7CD9`, `+0xA7CEA` are all **inside
`.text`**, i.e. they are *instruction immediates*, and they sit in two tight
pairs — the signature of a `1000.0f / maxfps` frame-time calculation.

* **Ctrl+G → `iw4mp.exe+A7B4F`** and read the surrounding code. Then look at
  `iw4mp.exe+A7CD9`.

The same pattern exists in single-player at `iw4sp.exe+0x9631F`, `+0x96330`,
`+0x964A9`, `+0x964BA` — so the limiter is shared code, which makes these four
MP sites very likely to be the limiter.

**Lead 2 — RETRACTED after inspecting the bytes.**

I originally flagged the single `85.0` (`+0x3698C4`) and `250.0` (`+0x3698D8`) as
a possible dvar registration (default 85, max 250). Dumping the surrounding bytes
shows they are just entries in a large **ascending general-purpose constant
table**:

```
0x00369884  0.007  0.0075  0.014  0.025  0.045  0.16  0.35  0.667
0x003698A4  3.141593  4.5  9  11  12.8  22  35  70
0x003698C4  85  87  110  135  225  250  300  315
0x003698E4  400  750  800  9000  50000  -3  -10  -14  ...
```

`85` and `250` are simply two rungs on that ladder (note `3.141593` = π). It is
not a cvar struct — **do not patch here.** The `1000.0` in `.rdata` at
`+0x3E4B24` is the same story: an ascending table of
`448, 480, 500, 512, 640, 672, 1000, 1024, 1200, 1440, 1728, 2500, 3000, 5000`.

**Lead 1 — confirmed real, now located precisely.**

The `.text` hits are genuine code: the tail of a small function that NaN-guards
two frame-timing globals.

```asm
; iw4mp.exe+0xA7B3F
movss   dword ptr [rip+0x44566B], xmm2
ucomiss xmm2, xmm1
jp      short +0C
jne     short +0A
mov     dword ptr [rip+0x44565D], 0x447A0000   ; <-- 1000.0f
ucomiss xmm6, xmm1
jp      short +0C
jne     short +0A
mov     dword ptr [rip+0x445650], 0x447A0000   ; <-- 1000.0f
```

A second, near-identical function sits at `+0xA7CD0`. Both write a pair of
adjacent float globals around **`iw4mp.exe+0x4ED1A8`**. Let x64dbg resolve the
exact target for you: open **`iw4mp.exe+A7B4F`** and read the operand shown on
the `mov dword ptr [rip+...], 0x447A0000` lines.

Those globals are frame-timing values reset to `1000.0` when the computed value
is invalid, which makes them **excellent hardware-breakpoint targets**: whatever
reads them every frame is the frame limiter.

**Next step (needs the game running).**
Launch `iw4mp.exe`, attach x64dbg to it, then:

1. **Ctrl+G → `iw4mp.exe+4ED1A8`**, select the address in the **Dump**,
   right-click → **Breakpoint → Hardware → Access**. The limiter will trip as
   soon as it reads the value.
2. In parallel, **Ctrl+G → `iw4mp.exe+38C3B8`** (the `com_maxfps` string),
   then right-click → **Search for → String references** to reach the dvar
   registration and any clamp applied to it.
3. Whatever clamp you find, patch the conditional jump with `type=bytes`,
   `patch=90 90` — or overwrite a writable float with `type=float`.

---

## SOLVED — the multiplayer FPS cap (confirmed working)

**The cap is the `com_maxfps` dvar, an INTEGER defaulting to 85. There is no 91,
and no float, anywhere in either binary.** That one fact explains why every
float hunt failed.

### Evidence (live `iw4mp.exe`, private match, no debugger)

Scanning the module image for an 8-byte pointer to the `com_maxfps` name string
(`iw4mp.exe+0x38C3B8`) found exactly one `dvar_t`. Its first field resolves back
to `com_maxfps`, which confirms it:

```
com_maxfps dvar @ 0x7FF758413BE0   (rva 0x6683BE0)
  +0x00  name pointer -> 'com_maxfps'
  +0x10  int 85   \
  +0x20  int 85    |  current / latched / default value slots (16 bytes apart)
  +0x30  int 85   /
  +0x44  int 100
  +0x48  int 11
```

Note the dump lists **no floats at all** — the value is an int. Searching for
`91.0` or `85.0` could never have found this.

### The fix

Write a larger integer into the three value slots (+0x10, +0x20, +0x30) with
`WriteProcessMemory` — no debugger, so no `int3`, no `STATUS_BREAKPOINT` crash,
and nothing that looks like a debugger to VAC.

**Result: confirmed working.** FPS jumped well above 85 instantly, and the value
was *not* re-clamped during play. That tells us the engine respects whatever sits
in the value slot and does not re-derive it every frame — so the write sticks.

### Persisting it — the config route does NOT work (tested)

I tried this properly so you don't have to:

* Added `seta com_maxfps "1000"` and `seta cg_fov "90"` to
  `players\config_mp.cfg`. Note the file carries the **read-only** attribute -
  clear it first or the write is denied with "access denied".
* Restarted the game and read the dvars back **before injecting anything**:

| dvar | config asked for | actual value on launch |
|------|------------------|------------------------|
| `com_maxfps` | `1000` | **85** |
| `cg_fov` | `90` | **65** |

Both were forced back to their defaults. The engine **clamps or ignores**
config-supplied values for these dvars, which is exactly why every community
"FPS unlocker" for this engine patches memory rather than using the config. It
also explains the `+0x44` fields (100 for `com_maxfps`, 80 for `cg_fov`): those
are the enforced limits.

**Conclusion: use the DLL.** Writing the value slot directly bypasses the clamp,
which is why the live write lifted the cap while the config line did nothing.

### The DLL route (implemented and verified)

The `dvar_int` / `dvar_float` feature types locate the live dvar by scanning for
a pointer to its name string, then write the value slots. Verified log from a
clean launch:

```
features: 'feature:mp_fov' name string reads as 'cg_fov'
features: 'feature:mp_fov' dvar located at 0x7FF7584220A0 (rva 0x66920A0)
features: 'feature:mp_maxfps' name string reads as 'com_maxfps'
features: 'feature:mp_maxfps' dvar located at 0x7FF758413BE0 (rva 0x6683BE0)
patcher: applied 'feature:mp_fov+0x10' (4 bytes at 0x7FF7584220B0)
patcher: applied 'feature:mp_fov+0x20' (4 bytes at 0x7FF7584220C0)
patcher: applied 'feature:mp_fov+0x30' (4 bytes at 0x7FF7584220D0)
patcher: applied 'feature:mp_maxfps+0x10' (4 bytes at 0x7FF758413BF0)
patcher: applied 'feature:mp_maxfps+0x20' (4 bytes at 0x7FF758413C00)
patcher: applied 'feature:mp_maxfps+0x30' (4 bytes at 0x7FF758413C10)
patches applied successfully
```

The dvar addresses are **re-discovered on every launch** — this session's
`cg_fov` was at a different address than the previous session's — which is
precisely why locating by pointer-to-name-string beats any hard-coded address.
The whole scan plus apply finishes in under a second.

> Tip: the DLL's log is opened with `_SH_DENYNO` so it can be read **while the
> game is running**. MSVC's plain `fopen` denies sharing, which makes the log
> unreadable exactly when you need it.

### Bonus: the FOV clamp

The same method found `cg_fov` as a **float** dvar:

```
cg_fov dvar @ 0x7FF75842A500
  +0x00  name pointer -> 'cg_fov'
  +0x10  float 65
  +0x20  float 65
  +0x30  float 65
  +0x40  float 65
  +0x44  float 80      <-- almost certainly the clamp (max FOV)
```

An FOV changer should therefore target that **80.0 clamp at `+0x44`**, and/or
write the value slots the same way as the FPS fix.

> **VAC warning — read this.** The official MW2 multiplayer is VAC-secured.
> Writing into `iw4mp.exe` during a secured session can get the account banned.
> The IW4x client (`iw4x.exe`) uses its own servers and is not VAC-secured, but
> it is 32-bit, so this x64 project does not apply to it. Decide which
> environment you care about before patching anything.

---

## The limiter quantises, and `0` really is uncapped

Measured on the same build by changing only `[fps] value`:

| `value` | observed FPS |
|---------|--------------|
| `250`   | ~206–216, **stable** |
| `167`   | ~190, **stable** |
| `0`     | **500+** |

So `com_maxfps` is a ceiling the engine *approximates*, not a precise cap. It
paces frames in millisecond steps: `1000 / 250` asks for a 4 ms frame, and the
pacing loop actually overshoots to roughly 4.7 ms, giving `1000 / 4.7 ≈ 212`.
The narrow, repeatable 206–216 band is the tell — a genuine hardware ceiling
would wander far more than 10 fps.

Consequences worth knowing:

* **There is no second limiter.** With `value=0` the game ran at 500+ with the
  same patches, so the ~212 was entirely our limiter's own granularity.
* **`value=0` is the only way to reach the real ceiling.** Everything else
  paces, and paces imprecisely.
* **The rule is `1000 / floor(1000 / request)`, plus sub-millisecond jitter.**
  The engine divides 1000 by the target with *integer* arithmetic to get a frame
  period in whole milliseconds, and the paced frames then overshoot that period
  by roughly 0.2-0.9 ms. Measured on the same build:

  | request | period used | overhead | achieved |
  |---------|-------------|----------|----------|
  | 125 | 8 ms (`1000/125 = 8.0`) | ~0.93 ms | ~112 |
  | 167 | **5 ms** (`1000/167 = 5.99` truncated) | ~0.26 ms | ~190 |
  | 250 | 4 ms (`1000/250 = 4.0`) | ~0.72 ms | ~212 |
  | 333 | 3 ms (`1000/333 = 3.003`) | ~0.23 ms | ~310 |

  This explains the odd case cleanly: **167 is not a clean divisor**, so it
  truncates to a 5 ms period and the game runs *faster* than requested - hence
  ~190 against a request of 167. It also explains why the community fixated on
  125 / 250 / 333 / 500 / 1000: those are precisely the requests whose period is
  a whole number of milliseconds, so they are the only ones that behave
  predictably.

  The jitter is the fatal part. It moves between ~0.2 and ~0.9 ms depending on
  load, so **no request lands exactly on its target** - and even if you found a
  request that hit 167 today, it would land somewhere else tomorrow. Do not use
  `com_maxfps` to select a specific frame rate.
* **For a precise cap, disable ours and use an external one** — the driver's
  max-frame-rate setting or RTSS — rather than fighting the engine's
  granularity. Set `com_maxfps` to `0` and let the external tool do the pacing.
* Uncapped costs power, heat and fan noise for frames no display can show.
  Capping is a legitimate choice, not a compromise — just know that the engine
  cannot cap accurately on its own.

This also explains why the community's old "FPS unlockers" for this engine were
never about finding a hidden constant: the constant is 85, it is an integer, and
the achievable rate above it is governed by the engine's own frame pacing.

---

## Why 125 / 167 / 250 / 333 are the magic numbers

They are not arbitrary, and the reason matters for how you use this tool.

Since movement is integrated per frame in this engine, holding a particular
frame rate changes the movement maths - you jump further and take less fall
damage at certain rates. The rates that "work" are exactly those whose frame
budget is a **whole number of milliseconds**:

| request | `1000 / request` | period the engine uses | measured |
|---------|------------------|------------------------|----------|
| 125 | 8.000 | 8 ms | ~112 |
| **167** | 5.988 | **5 ms** (truncated) | ~190 |
| 250 | 4.000 | 4 ms | ~212 |
| 333 | 3.003 | 3 ms | ~310 |
| 500 | 2.000 | 2 ms | not measured |
| 1000 | 1.000 | 1 ms | not measured |

Read that table against the folk wisdom, because it does not agree with it.

* **167 is not a clean divisor.** `1000 / 167` is 5.988, the engine truncates to
  a 5 ms period, and you end up at ~190 - *faster than you asked for*. 167 is a
  rate you **cannot** reach through `com_maxfps`, however often it is repeated.
* The same truncation hits the classic **91**: `1000 / 91` is 10.99, which
  truncates to 10 ms, so asking for 91 does not produce the 11 ms tick the
  number is supposed to encode. Treat that row as folklore until it is measured
  with the in-game counter.
* Only the exact divisors (125 / 250 / 500 / 1000) survive the arithmetic
  intact, which is why those are the numbers that behave predictably.

**The catch, and it's a big one.** Even the clean divisors drift, because the
pacing loop overshoots its own period by 0.2-0.9 ms depending on load - request
250 lands at ~212. The engine **cannot reliably sit on the very values that
matter**. For this purpose an imprecise rate is worse than useless, because the
whole effect depends on hitting the number exactly.

So for movement work:

1. Set `[fps] value=0`. That disables the engine's pacing entirely.
2. Hold the exact rate with an **external, precise limiter** - RTSS, or the
   driver's Max Frame Rate setting - set to the exact figure you want.

That combination is the point of this tool for this use case: `com_maxfps` at 0
to get the engine out of the way, and something with sub-millisecond precision
doing the pacing. Trying to reach a target through the engine's own limiter
means asking for roughly 15-18% more than you want and still drifting frame to
frame, which is precisely what breaks the effect.

> **This is a movement exploit, not a performance tweak.** It is fine in private
> matches, single player and offline testing, which is what this project was
> built for. On public or VAC-secured servers it is an unfair advantage and can
> be reported or actioned. Judge that before using it there.

---

## Correction: those readings came from the Steam overlay

Everything measured in the two sections above used the **Steam overlay's** frame
counter. That is not necessarily the same number as the one the engine believes
it is running at:

| Counter | What it actually counts |
|---------|-------------------------|
| Steam overlay / RTSS | frames *presented* to the compositor - what reached the GPU |
| `cg_drawFPS` | the rate the engine's own frame loop computes |

They can legitimately disagree. A presented frame can be dropped or duplicated
by the swap chain, and an overlay cannot see engine-side pacing at all. So the
`measured` column above describes a **presented** frame rate. The *shape* of the
model - integer-millisecond period, sub-millisecond overshoot, truncation - comes
from the arithmetic and is not in doubt, but the specific numbers should be
re-read from `cg_drawFPS` before being relied on for movement work.

To settle it, turn the in-game counter on and compare the two numbers in the
same session. That was the plan. It is possible in single player and impossible
in multiplayer - the next section explains why.

## The cvar is `cg_drawFPS`, not `cg_drawfps`

A case-sensitivity trap that cost real time here:

| searched | result |
|----------|--------|
| `cg_drawfps` (as every guide writes it) | **0 hits** |
| `cg_drawFPS` (as the binary writes it) | **1 hit** |
| `cg_drawFPSLabels` | present |
| `cg_drawfpslabels` | 0 hits |
| `com_maxfps` | present |
| `com_maxFPS` | 0 hits |

The capitalisation is inconsistent *within the same binary*: `com_maxfps` is
lowercase, `cg_drawFPS` is not. Since a cvar is located by matching its name's
exact bytes, a wrong guess does not fail loudly - it silently finds nothing,
which is exactly how the first attempt at this concluded "0 hits" and moved on
to the wrong target.

That is what `pattern::FindInsensitive()` is for. It lowercases the needle and
scans the region in 1 MB chunks, each chunk taken as `chunkSize + needleSize - 1`
bytes so a match straddling a chunk boundary is not missed, and it requires the
name's terminating null - so `cg_fov` still cannot match `cg_fovScale`. Both
spellings now work in the config, in both `iw4mp.exe` and `iw4sp.exe`.

---

## cg_drawFPS is dead in the multiplayer build

The counter was implemented, injected and did nothing. The write was not the
problem - the log proved the whole chain was sound:

```
features: 'drawfps' cvar name at 0x7FF7755E1028 (rva 0x371028) reads as 'cg_drawFPS'
features: 'drawfps' dvar located at 0x7FF77B90A6E0 (rva 0x669A6E0)
patcher: applied 'drawfps+0x10' (4 bytes at 0x7FF77B90A6F0)
features: 'drawfps' reads back +0x10 = int 1 (float 1.4013e-45)
```

The cvar was found, the dvar was a real entry in the same 0x60-byte array as
`com_maxfps` and `cg_fov` (968 and 609 entries away respectively), the value went
into all three slots, and no `re-applied` line ever appeared - unlike `fov`, the
engine was not even resetting it. It stayed 1. Nothing drew. Re-enabling it
before a level load did not change that either, which ruled out the obvious
"the HUD is built at level load" theory.

So the question became whether the value is read at all. That is answerable from
the binary, and the answer is no.

### The method

Every cvar is registered by a call that takes the name string and returns a
`dvar_t*`, which the caller caches in a static:

```
lea rcx, [cg_drawFPS]          ; the name string
call Dvar_Register...
mov [rip+disp32], rax          ; the dvar_t* is cached here
```

References to such a static are RIP-relative operands, and the target is always
`(address after the instruction) + disp32`. The instruction length therefore
does not matter for the arithmetic, so every reference can be found without
decoding a single opcode. Count them: the `mov` that stores the pointer is the
registration, and anything beyond it is a reader.

### The result

| executable | cvar | refs to the cached pointer | readers |
|------------|------|---------------------------|---------|
| `iw4mp.exe` | `cg_drawFPS` | 1 | **0** |
| `iw4mp.exe` | `cg_drawFPSLabels` | 1 | **0** |
| `iw4mp.exe` | `cg_drawViewpos` | 1 | **0** |
| `iw4mp.exe` | `drawLagometer` | 1 | **0** |
| `iw4mp.exe` | `lagometer` | 1 | **0** |
| `iw4sp.exe` | `drawLagometer` | - | not in the file at all |
| `iw4mp.exe` | `com_maxfps` | 2 | 1 (the frame limiter) |
| `iw4mp.exe` | `sv_network_fps` | 2 | 1 |
| `iw4mp.exe` | `cg_draw2D` | 4 | 3 (control: the HUD master switch) |
| `iw4mp.exe` | `cg_drawCrosshair` | 2 | 1 (control) |
| `iw4sp.exe` | `cg_drawFPS` | 7 | **6** |

`com_maxfps` is the control that makes this trustworthy: exactly one reader, the
limiter - the one place a frame cap has to be applied - and writing it
demonstrably works. Read HUD cvars behave the same way: `cg_draw2D` (the master
HUD switch) has three readers and `cg_drawCrosshair` has one, so a cvar that is
read does show up in this count. The entire debug-HUD block shows zero.

The pattern is consistent enough to name. The 64-bit multiplayer client still
*registers* these cvars - so they appear in cvar lists, and a config will
happily save them - but the code that would read them is not in the binary.
Registering a cvar and using it are two separate acts, and only the second one
is visible from outside the process.

The multiplayer client registers the whole debug-HUD block and then reads none
of it. `cg_drawFPS` is a dead cvar there. No console command, no config edit and
no memory write from this tool can make it draw, because the code that would
read it is not in the binary.

### What the values would have been

The cvar's domain is an enum, readable from the struct: the field after the
value slots is `{count, const char**}` and it points at a NULL-terminated list of
names in `.rdata` - `Off`, `Simple`, `SimpleRanges`, `Verbose`,
`Verbose+Viewpos`. So the counter takes 0-4, not 0/1. It also means the enum
*data* is present in the multiplayer build even though the code that would use
it is not, which is exactly what makes this trap hard to spot from outside.

### What this means

* **Multiplayer has no on-screen frame counter.** Not through `cg_drawFPS`, not
  through `cg_drawFPSLabels`, not through `cg_drawViewpos`. In multiplayer the
  engine's own frame rate has to be measured from outside the process.
* **`sv_network_fps` is alive in multiplayer** and is the only counter that can
  be put on the HUD there - but it reports the network/snapshot rate, not the
  render frame rate. It sits far lower than your fps, and it is the figure the
  classic `91` cap was actually about.
* **Single player is unaffected.** `iw4sp.exe` reads `cg_drawFPS` from six
  places, so the counter works there. `[drawfps]` ships disabled and is worth
  enabling only for campaign and Spec Ops.

The general lesson: a registered cvar that nothing reads looks identical from
outside to a feature that is broken. The check above separates the two, and it
is the difference between "my offset is wrong" and "this build does not have the
feature".
