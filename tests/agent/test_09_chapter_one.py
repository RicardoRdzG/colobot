"""
test_09 — Complete every level in Exercises Chapter 1 without cheats.

For each of the 7 levels the test:
  1. Launches the level directly via /launch.
  2. Opens the Studio (reuses an existing matching slot when possible).
  3. Writes the official solution script (sourced from the game's *.txt files).
  4. Clicks StudioRun (compile + start in one action) then StudioOK.
  5. Waits for the LevelComplete screen (all chapter-1 exercises have EndingFile).

Solutions come from the level data files, e.g.
  data/levels/exercises/chapter001/level001/tspid1.txt
They are reproduced here verbatim so the test has no filesystem dependency.

Each test is independent — /launch resets the level so there is no shared state.
"""

import sys
import os

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from agent_client import AgentClient
from exercise_helpers import run_exercise_level

# ---------------------------------------------------------------------------
# Official solution scripts (verbatim from the game data *.txt files)
# ---------------------------------------------------------------------------

# level001 — Spiders 1  (tspid1.txt)
# Aim at each of the three alien spiders in turn and fire.
TSPID1 = """extern void object::Example()
{
\taim(0);
\tfire(1);

\tturn(90);
\tfire(1);

\tturn(-180);
\tfire(1);
}
"""

# level002 — Power Cell 1  (tcell1.txt)
# Pick up a power cell from one robot and give it to another.
TCELL1 = """extern void object::Example()
{
\tgrab();
\tturn(90);
\tdrop();

\tturn(-180);

\tgrab();
\tturn(90);
\tdrop();
}
"""

# level003 — Titanium 1  (ttit1.txt)
# Drive forward, collect titanium ore, return and deposit.
TTIT1 = """extern void object::Example()
{
\tmove(20);
\tgrab();

\tturn(180);
\tmove(30);
\tdrop();
\tmove(-3);
}
"""

# level004 — Titanium 2  (ttit2.txt)
# Use radar to find ore and converter; navigate and deposit.
TTIT2 = """extern void object::Example()
{
\tobject    item;

\titem = radar(TitaniumOre);
\tgoto(item.position);
\tgrab();

\titem = radar(Converter);
\tgoto(item.position);
\tdrop();
\tmove(-3);
}
"""

# level005 — Power Cell 2  (tcell2.txt)
# Continuously find power cells and ferry them to winged shooters.
TCELL2 = """extern void object::Example()
{
\tobject    item;

\twhile(true)
\t{
\t\titem = radar(PowerCell);
\t\tgoto(item.position);
\t\tgrab();

\t\titem = radar(WingedShooter);
\t\tgoto(item.position);
\t\tdrop();
\t}
}
"""

# level006 — Spiders 2  (tspid2.txt)
# Continuously radar nearest spider, turn to face it, and fire.
TSPID2 = """extern void object::Example()
{
\tobject    item;

\twhile (true)
\t{
\t\titem = radar(AlienSpider);
\t\tturn(direction(item.position));
\t\tfire(1);
\t}
}
"""

# level007 — Spiders 3  (tspid3.txt)
# Radar spider, turn, advance to firing range, then fire.
TSPID3 = """extern void object::Example()
{
\tobject    item;

\twhile (true)
\t{
\t\titem = radar(AlienSpider);
\t\tturn(direction(item.position));
\t\tmove(distance(position, item.position)-40);
\t\tfire(1);
\t}
}
"""

# ---------------------------------------------------------------------------
# Test parameters: (rank, title, source, timeout_seconds)
# Timeout is generous — CI machines can be slow and tcell2 loops until
# all four WingedShooters are fuelled.
# ---------------------------------------------------------------------------

LEVELS = [
    (1, "Spiders 1",    TSPID1, 40),
    (2, "Power Cell 1", TCELL1, 40),
    (3, "Titanium 1",   TTIT1,  40),
    (4, "Titanium 2",   TTIT2,  90),   # goto() pathfinds around quartz clusters
    (5, "Power Cell 2", TCELL2, 90),   # while(true) loop; ends when all bots fuelled
    (6, "Spiders 2",    TSPID2, 45),
    (7, "Spiders 3",    TSPID3, 60),
]


# ---------------------------------------------------------------------------
# Parametrized test
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(
    "rank,title,source,win_timeout",
    LEVELS,
    ids=[f"level{r:03d}-{t.lower().replace(' ', '_')}" for r, t, _, _ in LEVELS],
)
def test_exercise_chapter1_complete(
    client: AgentClient,
    rank: int,
    title: str,
    source: str,
    win_timeout: int,
):
    """Launch the level, inject the official solution, and verify it wins."""
    run_exercise_level(client, "Exercises", 1, rank, source, win_timeout, title=title)
