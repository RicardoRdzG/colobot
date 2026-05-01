"""
test_10 — Complete all exercise levels in chapters 2-7 using official solutions.

Levels are discovered at collection time by scanning the build-dev data directory.
Each level's soluce script is read from data/ai/<script>.txt and injected via Studio.

Chapter 1 is already covered by test_09_chapter_one.py.

Some official scripts are correct but very slow (blocking turn()+wait(0.2) loops).
Those levels have optimized source overrides in _SOURCE_OVERRIDES that use
differential motor steering — the robot curves continuously toward the target
instead of stopping to rotate, cutting completion time 5× while using the same
game mechanics.

Some levels have known script bugs that only manifest under test conditions:
 - ch6/lvl3 (Remote Control #2): tremot2a.txt sends "order" before "param" over
   ExchangePost; the slave can read both in separate CBot ticks at high speed,
   seeing param=0 instead of the real value. Override sends "param" first.
 - ch7/lvl2 (Remote Control #4): tremot4b.txt uses `int m_type = nan` as a
   sentinel. CBot's VarIsNAN() returns false for int vars, so put() always
   returns false and the slave's wait condition is always true → deadlock.
   Override replaces both slave (via prepare hook) and controller with an
   equivalent protocol that uses -1 as the int sentinel instead of nan.
"""

import re
import time
import sys
import os
from pathlib import Path

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from agent_client import AgentClient
from exercise_helpers import run_exercise_level

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

_REPO_ROOT = Path(__file__).parent / "../.."
_EXERCISES_DIR = _REPO_ROOT / "build-dev/data/levels/exercises"
_AI_DIR        = _REPO_ROOT / "data/ai"

# Regex: matches the trainer-robot CreateObject line carrying both the script
# name and the scriptRunnable=false flag, e.g.:
#   script4="tant1.txt" scriptRunnable4=false
_SOLUCE_RE = re.compile(r'script(\d+)="([^"]+)"\s+scriptRunnable\1=false')

# ---------------------------------------------------------------------------
# Per-level source overrides — defined BEFORE _discover_exercises() is called.
#
# Key: (chap, rank).  Value: (replacement_source, timeout_seconds).
#
# Wasp Hunter 1 & 2 (ch2/lvl6-7): the official twasp1/2.txt scripts use a
# blocking turn()+wait(0.2) approach that takes ~165s per first-kill and ~250s
# total.  The differential-steering version below keeps the robot moving at all
# times, curving toward the nearest wasp via motor asymmetry; it clears all
# four wasps in ~52s.
# ---------------------------------------------------------------------------

_TWASP_DIFF = """\
extern void object::Example()
{
\tobject    item;
\tfloat     angle;

\taim(0);

\twhile (true)
\t{
\t\titem = radar(AlienWasp);
\t\tif(item == null) break;

\t\tangle = direction(item.position);

\t\t// Differential steering: keep moving, curve toward wasp
\t\tif(angle > 30)       motor(0.0, 1.0);
\t\telse if(angle > 5)   motor(0.6, 1.0);
\t\telse if(angle < -30) motor(1.0, 0.0);
\t\telse if(angle < -5)  motor(1.0, 0.6);
\t\telse                 motor(1.0, 1.0);

\t\t// Altitude match
\t\tif(position.z > item.position.z + 0.5)    jet(-0.3);
\t\telse if(position.z < item.position.z - 1) jet(0.3);
\t\telse                                        jet(0);

\t\t// Fire when close and roughly aligned
\t\tif(distance(position, item.position) < 25 && angle > -15 && angle < 15)
\t\t{
\t\t\tfire(0.5);
\t\t}

\t\twait(0.1);
\t}
}
"""

# ch3/lvl6 Labyrinth 1 — official tlaby1.txt teaches one single-step iteration
# (no loop), so it only moves one cell and the level never completes.
# We wrap it in a while(true) loop identical to tlaby2.txt's approach.
_TLABY_LOOP = """\
extern void object::Example()
{
\twhile (true)
\t{
\t\tobject front, left, right;

\t\tfront = radar(Barrier,   0, 45, 0, 5);
\t\tleft  = radar(Barrier,  90, 45, 0, 5);
\t\tright = radar(Barrier, -90, 45, 0, 5);

\t\tif (front == null) { move(5); continue; }
\t\tif (left  == null) { turn(90); continue; }
\t\tif (right == null) { turn(-90); continue; }
\t\tbreak;
\t}
}
"""

# ch6/lvl3 Remote Control #2 — official tremot2a.txt sends "order" before "param"
# over ExchangePost. At high simulation speed CBot ticks can interleave between the
# two send() calls: the slave sees testinfo("order")==true, reads param=0 (not yet
# written), executes move(0)/turn(0), then deleteinfo. Override swaps send order so
# "param" is always present when the slave wakes on "order".
_TREMOT2A_FIXED = """\
extern void object::Remote( )
{
\tSendToPost(1, 20);  // move(20)
\tSendToPost(2, 90);  // turn(90)
\tSendToPost(1, 20);  // move(20)
\tSendToPost(2, 90);  // turn(90)
\tSendToPost(1, 10);  // move(10)
\tSendToPost(2, 90);  // turn(90)
\tSendToPost(1, 10);  // move(10)
\tSendToPost(2,-90);  // turn(-90)
\tSendToPost(1, 10);  // move(10)
}

void object::SendToPost(float order, float param)
{
\t// send "param" before "order" so the slave can never read param=0 by racing
\t// between the two send() calls from the original script.
\tsend("param", param, 100);
\tsend("order", order, 100);

\twhile ( testinfo("order", 100) )
\t{
\t\twait(1);
\t}
}
"""

# ch7/lvl2 Remote Control #4 — official tremot4b.txt uses `int m_type = nan` as a
# sentinel.  CBot's VarIsNAN() returns false for int vars (only float/double support
# nan), so the exchange.put() condition `m_order.m_type == nan` is always false →
# put() always returns false.  Simultaneously the slave's wait condition
# `todo.m_type != nan` is always true, so it loops on "Unknown order" forever.
#
# Fix: redefine the exchange/order classes using -1 as the int sentinel (a value that
# compares correctly with ==).  Both the fixed slave (injected by _prep_ch07_lvl02)
# and the fixed controller below use the renamed classes (order2 / exchange2) so they
# don't clash with the auto-compiled originals.
_TREMOT4B_FIXED = """\
public class order2
{
\tint    m_type  = -1;
\tfloat  m_param = 0;
}

public class exchange2
{
\tstatic private order2 m_order = new order2;

\tsynchronized bool put(order2 a)
\t{
\t\tif (m_order.m_type == -1) { m_order = a; return true; }
\t\telse                       { return false; }
\t}

\tsynchronized order2 get()  { return m_order; }
\tsynchronized void  remove() { m_order.m_type = -1; }
}

extern void object::Slave4()
{
\texchange2 list();
\torder2    todo;

\twhile (true)
\t{
\t\twhile (true)
\t\t{
\t\t\ttodo = list.get();
\t\t\tif (todo.m_type != -1) break;
\t\t\twait(1);
\t\t}

\t\tif (todo.m_type == 1) { message("move("+todo.m_param+")"); move(todo.m_param); }
\t\telse if (todo.m_type == 2) { message("turn("+todo.m_param+")"); turn(todo.m_param); }
\t\tlist.remove();
\t}
}
"""

_TREMOT4A_FIXED = """\
extern void object::Remote4()
{
\tSendOrder(1, 20);
\tSendOrder(2, 90);
\tSendOrder(1, 20);
\tSendOrder(2, 90);
\tSendOrder(1, 10);
\tSendOrder(2, 90);
\tSendOrder(1, 10);
\tSendOrder(2,-90);
\tSendOrder(1, 10);
}

void object::SendOrder(float order, float param)
{
\texchange2 list();
\torder2    todo();
\ttodo.m_type  = order;
\ttodo.m_param = param;

\twhile (list.put(todo) == false) { wait(1); }
}
"""

_SOURCE_OVERRIDES: dict = {
    (2, 6): (_TWASP_DIFF, 120),       # Wasp Hunter 1
    (2, 7): (_TWASP_DIFF, 120),       # Wasp Hunter 2
    (3, 6): (_TLABY_LOOP, 120),       # Labyrinth 1 — official script has no loop
    (6, 3): (_TREMOT2A_FIXED, 120),   # Remote Control #2 — ExchangePost race condition
    (7, 2): (_TREMOT4A_FIXED, 120),   # Remote Control #4 — int/nan CBot bug
}

# Per-level timeout-only overrides (official source is kept, just the deadline extended).
# ch3/lvl5: receives ~520m of waypoint data over ExchangePost — 120s is too short.
# ch3/lvl8: 36 sniff() + move() iterations at ~3s each — 120s is too short.
# ch4/lvl4: waits for a randomly-moving TargetBot to stand still before firing;
#            worst-case wandering can exceed 120s.
_TIMEOUT_OVERRIDES: dict = {
    (3, 5): 200,   # Exchange posts 2
    (3, 8): 200,   # The gold digger
    (4, 4): 200,   # Patient hunter — random target movement
}

# ---------------------------------------------------------------------------
# Per-level preparation hooks
#
# Some exercises require the astronaut to physically install equipment on
# the trainer bot before the robot script can run (e.g. ch3/lvl1 spawns
# the trainer with power=-1 and a loose PowerCell on the ground — the
# student is expected to walk Me over and place the cell on the bot's
# rear). The hook runs after /launch but before open_studio().
#
# Hook signature: hook(client) -> bool. Return False to fail-skip the
# test with a descriptive message.
# ---------------------------------------------------------------------------

def _prep_install_powercell(client: AgentClient) -> bool:
    """Walk Me to the loose PowerCell, pick it up, drop it on the bot's rear."""
    return client.me_pickup_and_install("PowerCell", "WheeledTrainer")


def _prep_ch07_lvl02_slave(client: AgentClient) -> bool:
    """Replace the broken slave on PracticeBot with a working exchange2/order2 version.

    Official tremot4b.txt uses `int m_type = nan` as a sentinel.  CBot's VarIsNAN()
    returns false for int variables, making put() always return false and the slave's
    wait condition always true — a permanent deadlock.

    The PracticeBot has run=1 so the broken slave auto-starts, disabling
    ButtonAddProgram.  We open Studio directly on the running slot: StudioRun stops
    the running slave (toggles stop when already running), then we inject the fixed
    source and StudioRun again to compile and start it.
    """
    def _is_practice_bot(c: AgentClient) -> bool:
        try:
            src = c.get_program(0).get("source", "")
            return "Slave4" in src or "m_type" in src
        except Exception:
            return False

    if not client.open_studio(robot_filter=_is_practice_bot):
        return False

    try:
        # If the broken slave is already running (run=1 auto-start), stop it first
        # so the next StudioRun compiles-and-starts rather than just stops.
        if client.diagnostics().get("running"):
            client.click("StudioRun")   # stop the broken slave
            time.sleep(0.3)
            client.dismiss_satcom()
        edit = client.find_widget("StudioEdit")
        if not edit or edit.get("value", "").strip() != _TREMOT4B_FIXED.strip():
            client.type("StudioEdit", _TREMOT4B_FIXED)
            time.sleep(0.2)
        client.click("StudioRun")   # compile and start fixed slave
        time.sleep(0.3)
        client.dismiss_satcom()
        client.click("StudioOK")
        client.wait_for_screen("InGame", timeout=5)
        try:
            client.set_speed(16.0)   # Studio resets speed; restore for the main test
        except Exception:
            pass
        return True
    except Exception:
        return False


_PREPARE_HOOKS: dict = {
    (3, 1): _prep_install_powercell,   # Follow a path — bot starts with no power
    (7, 2): _prep_ch07_lvl02_slave,    # Remote Control #4 — replace broken slave
}

# ---------------------------------------------------------------------------
# Level discovery — runs at collection time, not at runtime
# ---------------------------------------------------------------------------


def _discover_exercises(skip_chapters=(1,)):
    """Return a list of (chap, rank, title, source, timeout) for chapters not in skip_chapters."""
    levels = []
    for ch_dir in sorted(_EXERCISES_DIR.glob("chapter*")):
        chap = int(ch_dir.name.replace("chapter", ""))
        if chap in skip_chapters:
            continue
        for lvl_dir in sorted(ch_dir.glob("level*")):
            rank = int(lvl_dir.name.replace("level", ""))
            scene_path = lvl_dir / "scene.txt"
            if not scene_path.exists():
                continue
            scene = scene_path.read_text()

            title_m = re.search(r'Title\.E text="([^"]+)"', scene)
            title = title_m.group(1) if title_m else f"Level {rank}"

            soluce_m = _SOLUCE_RE.search(scene)
            if not soluce_m:
                continue   # no soluce — skip (shouldn't happen in exercises)
            script_name = soluce_m.group(2)
            script_path = _AI_DIR / script_name
            if not script_path.exists():
                continue   # script not in data submodule yet
            source = script_path.read_text()

            # Generous timeout: some levels use goto() pathfinding over long
            # distances or while(true) loops that end when a win condition fires.
            timeout = 120

            # Apply per-level source/timeout override when present.
            if (chap, rank) in _SOURCE_OVERRIDES:
                source, timeout = _SOURCE_OVERRIDES[(chap, rank)]
            if (chap, rank) in _TIMEOUT_OVERRIDES:
                timeout = _TIMEOUT_OVERRIDES[(chap, rank)]

            levels.append((chap, rank, title, source, timeout))
    return levels


_EXERCISE_LEVELS = _discover_exercises(skip_chapters=(1,))


# ---------------------------------------------------------------------------
# Parametrized test
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(
    "chap,rank,title,source,win_timeout",
    _EXERCISE_LEVELS,
    ids=[
        f"ex_ch{c:02d}_lvl{r:02d}-{t.lower().replace(' ', '_')[:20]}"
        for c, r, t, _, _ in _EXERCISE_LEVELS
    ],
)
def test_exercise_complete(
    client: AgentClient,
    chap: int,
    rank: int,
    title: str,
    source: str,
    win_timeout: int,
):
    """Launch the level, inject the official solution (or optimized override), and verify it wins."""
    run_exercise_level(
        client, "Exercises", chap, rank, source, win_timeout,
        title=title,
        prep_hook=_PREPARE_HOOKS.get((chap, rank)),
    )
