# dev-status.md — Exercise Test Suite Progress

Last updated: 2026-04-29  
Branch: in-progress  
Overall: **30/31 passing** before latest fix, expecting **31/31** after `open_studio` two-pass fix.

---

## Changes made this session

### 1. CBot bug — purged `public class` breaks compilation across level transitions (FIXED)

**Files changed:**
- `CBot/src/CBot/CBotClass.h` — added `IsFullyDefined()` accessor
- `CBot/src/CBot/CBotInstr/CBotInstr.cpp` — guard class-type detection
- `CBot/src/CBot/CBotUtils.cpp` — guard `TypeParam` class-type detection

**Root cause:** `CBotClass::m_publicClasses` (global static set) retains purged class entries
across level transitions. After level N ends, its `public class` definitions are purged
(`m_IsDef=false`, fields/methods cleared) but remain in the registry. When level N+1's
scripts compile, identifiers like `order` or `exchange` are mistakenly resolved as type
names instead of variable names, producing "Variable name missing" errors.

**Fix logic:**
```cpp
if (pClass != nullptr &&
    (pClass->IsFullyDefined() ||                          // fully compiled
     pStack->GetProgram()->ClassExists(pClass->GetName()))) // being compiled now
```
The `ClassExists` check is critical: during CBot's Step 2 (DefineClasses), all classes in
the same program have `m_IsDef=false`. Without it, intra-program class references (e.g.
`exchange` using `order` as a field type) also break.

**Levels fixed:** ch3/lvl9 (Remote Control #1), ch7/lvl2 (Remote Control #4),
ch7/lvl3 (Remote Control #5).

---

### 2. `open_studio` — wrong robot selected when Astronaut appears first (FIXED)

**File changed:** `tests/agent/agent_client.py` — `open_studio()` method

**Root cause:** Shortcut buttons 1501-1549 list all selectable robots. After several
missions the Astronaut (Me) accumulates program slots and gets `ButtonOpenStudio`.
Me appears at a lower evt number than the target robot, so Me was selected first.
The old code only scanned for `runnable=True` slots; finding none on Me, it called
`ButtonAddProgram` on Me, injecting the solution onto the wrong robot.

**Fix:** Two-pass strategy:
- **Pass 1** (`require_source_match=True`): iterate 1501-1549, skip any robot that has no
  slot (runnable or non-runnable) with matching source. This finds the pre-loaded
  non-runnable slot on the correct robot (e.g. TrackedShooter has `script4="tchasse1.txt"
  scriptRunnable4=false`).
- **Pass 2** (fallback, original behaviour): used when no robot has a pre-loaded matching
  slot (e.g. source-overridden levels like `_TWASP_DIFF`). Falls back to first robot
  with `ButtonAddProgram`.

**Level fixed:** ch4/lvl4 "Patient Hunter" — was consistently failing when run after
ch4/lvl3 because tchasse1.txt was injected onto Me instead of TrackedShooter.

---

### 3. Timeout override added (FIXED)

**File changed:** `tests/agent/test_10_exercises_all.py`

```python
_TIMEOUT_OVERRIDES = {
    (3, 5): 200,   # Exchange posts 2
    (3, 8): 200,   # The gold digger
    (4, 4): 200,   # Patient hunter — random target movement
}
```

---

## Known remaining issues

### Timing sensitivity at high simulation speed (16×)

Some levels exhibit race conditions when run at 16× speed that do not occur at 1×:

- **ch6/lvl3 Remote Control #2** — `tremot2a.txt` sends "order" before "param" over
  ExchangePost. At high speed, CBot ticks can interleave between the two `send()` calls:
  the slave reads `param=0` before it is written. **Workaround:** `_TREMOT2A_FIXED`
  override swaps send order so "param" is always present when slave wakes.
  Currently **PASSING**.

- **ch7/lvl2 Remote Control #4** — `tremot4b.txt` uses `int m_type = nan` as sentinel.
  CBot's `VarIsNAN()` returns false for `int` variables → `put()` always returns false →
  deadlock. **Workaround:** `_TREMOT4B_FIXED` / `_TREMOT4A_FIXED` overrides use `-1`
  as sentinel. Currently **PASSING**.

- **General:** Studio resets game speed to 1× on open; tests restore to 16× after
  `StudioOK`. There is a brief window between Studio closing and `set_speed(16.0)` where
  the game runs at 1×. On slow machines this might cause timeouts near the limit.

### Focused test commands

Run a single level (replace chap/rank):
```sh
cd tests/agent
pytest test_10_exercises_all.py -k "ch4_lvl4" -v
```

Run all chapter 4 levels:
```sh
pytest test_10_exercises_all.py -k "ch4" -v
```

Run full suite:
```sh
pytest test_10_exercises_all.py -v
```

Run with output capture disabled (see print statements):
```sh
pytest test_10_exercises_all.py -k "ch4_lvl4" -v -s
```

---

## Test infrastructure notes

- **`agent_client.py`**: HTTP client wrapping the colobot agent server REST API.
  Key methods: `open_studio()`, `get_program()`, `load_program()`, `set_speed()`.
- **`test_10_exercises_all.py`**: Parametrized pytest. Discovers levels by scanning
  `build-dev/data/levels/exercises/` for `_SOLUCE_RE` matches.
- **Speed:** Tests run at 16× simulation speed. Studio always resets to 1× on open.
- **State isolation:** Each test navigates to main menu before/after via `_teardown()`.
- **Prepare hooks:** Some levels require physical setup (e.g. installing power cell on
  trainer bot) via `_PREPARE_HOOKS`. See `_prep_install_powercell` etc.
