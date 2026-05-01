# dev-status.md — Exercise Test Suite Progress

Last updated: 2026-05-01 (all 31 passing)
Branch: `claude/awesome-napier-90fbf9`
CBot fixes branch: `fix/cbot-purged-class-nan`

---

## Current status

| Suite | Expected | Last verified |
|-------|----------|---------------|
| test_09 (ch1, 7 levels) | 7/7 | 2026-04-26 |
| test_10 (ch2–7, 31 levels) | 31/31 | 2026-05-01 |

> Run `pytest tests/agent/ -v` to verify.

---

## Bugs fixed this branch

### 1. CBot — purged `public class` breaks compilation across level transitions
**Files:** `CBot/src/CBot/CBotClass.h`, `CBotInstr.cpp`, `CBotUtils.cpp`

After a level ends, public classes are purged (`m_IsDef=false`) but stay in
`m_publicClasses`. The parser mistook stale entries as known types. Fixed with a
two-condition guard: accept a class name only if fully compiled **or** belongs to
the currently-compiling program (`ClassExists`).

Levels unblocked: ch3/lvl9, ch7/lvl2, ch7/lvl3.

### 2. CBot — `int m_type = nan` sentinel always evaluates false
**Files:** `CBotVar.h`, `CBotVarValue.h`, `CBotTwoOpExpr.cpp`

`VarIsNAN()` only detected NaN on float variables. Assigning `nan` to an `int`
silently truncated to 0. Added `InitType::NAN_INT = 3`; integer SetValFloat sets
it when `std::isnan()`; VarIsNAN checks for it.

Level unblocked: ch7/lvl2 (also has script override using `-1` sentinel).

### 3. `open_studio` — wrong robot selected (Astronaut before target)
**File:** `tests/agent/agent_client.py`

Shortcut buttons list all selectable robots. After several missions the Astronaut
accumulates slots and appeared before the target robot. Single-pass rewrite:
- Prefers robots with a source-matching slot (runnable first, non-runnable accepted).
- Anti-pollution fallback: reuses existing runnable slot instead of `ButtonAddProgram`.
- `robot_filter` predicate lets prepare hooks target by script content.

Level unblocked: ch4/lvl4 and any level run after Me has accumulated slots.

### 4. `open_studio` — slave robot selected instead of controller (ch6/lvl3, ch7/lvl1, ch7/lvl3)
**File:** `tests/agent/agent_client.py`

`currently_running` detection used `ButtonStopProgram` (EVENT_OBJECT_PROGSTOP), which
is never created as a toolbar widget. So robots with auto-started slaves (run=1) appeared
as idle. The first shortcut button (often the slave) became `fallback_evt`, and Studio
opened on the slave — overwriting the slave script with the controller, then failing to
compile because the `exchange` class was purged from that program's context.

Fix: detect running via `ButtonAddProgram.enabled` — the interface sets
`bProgEnable = !IsProgram()`, disabling ButtonAddProgram when a script is running.

### 5. ExchangePost race condition at 16× speed
**File:** `tests/agent/test_10_exercises_all.py` (`_TREMOT2A_FIXED`)

Official `tremot2a.txt` sends `"order"` then `"param"`. At high speed the slave
can read `param=0` between the two sends. Override sends `"param"` first.

Level fixed: ch6/lvl3.

---

## Per-level overrides

### Source overrides (`_SOURCE_OVERRIDES`)

| Level | Script | Reason |
|-------|--------|--------|
| ch2/lvl6–7 | `_TWASP_DIFF` | Official `twasp1/2.txt` uses blocking `turn()+wait(0.2)` — ~250 s. Differential-steering version clears in ~52 s. |
| ch3/lvl6 | `_TLABY_LOOP` | Official `tlaby1.txt` has no loop — only moves one cell and stops. |
| ch6/lvl3 | `_TREMOT2A_FIXED` | ExchangePost race at high speed (see above). |
| ch7/lvl2 | `_TREMOT4A_FIXED` + `_TREMOT4B_FIXED` | int/nan sentinel deadlock (see above). |

### Timeout overrides (`_TIMEOUT_OVERRIDES`)

| Level | Timeout | Reason |
|-------|---------|--------|
| ch3/lvl5 | 200 s | ~520 m of waypoint data over ExchangePost |
| ch3/lvl8 | 200 s | 36 sniff()+move() iterations at ~3 s each |
| ch4/lvl4 | 200 s | TargetBot wanders randomly; worst-case >120 s |

---

## Test infrastructure

- **Speed:** `set_speed(16.0)` called after level spawn and again after `StudioOK`
  (Studio resets speed to 1× on open — confirmed in `studio.cpp:573`).
- **SatCom:** After `StudioRun`, poll up to 0.5 s for SatCom to appear and dismiss
  it before clicking `StudioOK` (replaces fixed `sleep(0.3)`).
- **Robot spawn:** Poll for `evt:15*` shortcut buttons before starting (replaces
  fixed `sleep(1.2)`).
- **Slot anti-pollution:** `open_studio` reuses existing runnable slots; only calls
  `ButtonAddProgram` when truly no usable slot exists.
- **Shared library:** `exercise_helpers.py` — `await_win`, `teardown_to_main_menu`,
  `run_exercise_level`. Both test_09 and test_10 import from it.

---

## Focused test commands

```sh
# Single level
pytest tests/agent/test_10_exercises_all.py -k "ch04_lvl04" -v -s

# Full chapter
pytest tests/agent/test_10_exercises_all.py -k "ex_ch03" -v -s

# test_09 only (ch1)
pytest tests/agent/test_09_chapter_one.py -v

# Full exercise suite
pytest tests/agent/test_09_chapter_one.py tests/agent/test_10_exercises_all.py -v

# All agent tests
pytest tests/agent/ -v
```
