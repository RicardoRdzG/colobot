"""Shared helpers for exercise-level tests.

Used by test_09_chapter_one.py and test_10_exercises_all.py.
"""

import time
from typing import Callable, Optional

import pytest

from agent_client import AgentClient


def await_win(client: AgentClient, timeout: float, label: str) -> str:
    """Poll /state until LevelComplete; raise pytest.fail on timeout."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        screen = client.state()["screen"]
        if screen == "LevelComplete":
            return screen
        time.sleep(0.2)
    pytest.fail(
        f"{label}: LevelComplete not reached within {timeout}s "
        f"(last screen: {client.state()['screen']})"
    )


def teardown_to_main_menu(client: AgentClient) -> None:
    """Drive back to MainMenu from any in-session screen."""
    try:
        client.set_speed(1.0)
    except Exception:
        pass
    try:
        screen = client.state()["screen"]
        if screen == "SatCom":
            client.click("SatComClose")
            client.wait_for_screen("InGame", timeout=5)
            screen = "InGame"
        if screen == "Studio":
            client.click("StudioCancel")
            client.wait_for_screen("InGame", timeout=5)
            screen = "InGame"
        if screen == "LevelComplete":
            client.click("ButtonEndLevel")
            client.wait_for_screen("LevelSelect", timeout=10)
            screen = "LevelSelect"
        if screen == "LevelSelect":
            client.click("ButtonBack")
            client.wait_for_screen("MainMenu", timeout=10)
        elif screen == "InGameMenu":
            client.click("ButtonAbort")
            client.wait_for_screen("MainMenu", timeout=10)
        elif screen == "InGame":
            client.key("Escape")
            client.wait_for_screen("InGameMenu", timeout=5)
            client.click("ButtonAbort")
            client.wait_for_screen("MainMenu", timeout=10)
    except Exception:
        pass


def run_exercise_level(
    client: AgentClient,
    category: str,
    chap: int,
    rank: int,
    source: str,
    win_timeout: float,
    title: str = "",
    prep_hook: Optional[Callable[[AgentClient], bool]] = None,
) -> None:
    """Full exercise level flow: navigate → launch → prepare → inject solution → assert win.

    Raises pytest.skip if preparation or robot selection fails.
    Raises pytest.fail if LevelComplete is not reached within win_timeout.
    Always returns to MainMenu on exit (success or failure).
    """
    from conftest import navigate_to_main_menu

    label = f"{category}/ch{chap}/lvl{rank}" + (f" ({title})" if title else "")

    navigate_to_main_menu(client)
    client.launch(category, chap, rank)
    client.wait_for_screen("InGame", timeout=30)

    # Wait for robot shortcut buttons to appear (physics/spawn settle).
    # Break early if the level auto-opens SatCom (briefing) — that also signals
    # the scene is ready.
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        s = client.state()
        if s.get("screen") == "SatCom":
            break
        if any(w.get("id", "").startswith("evt:15") for w in s.get("widgets", [])):
            break
        time.sleep(0.1)

    client.set_speed(16.0)
    client.dismiss_satcom()  # close auto-briefing if the level opened it

    if prep_hook is not None and not prep_hook(client):
        teardown_to_main_menu(client)
        pytest.skip(f"{label}: prepare hook failed")

    if not client.open_studio(expected_source=source):
        teardown_to_main_menu(client)
        pytest.skip(f"{label}: no programmable robot found")

    try:
        edit = client.find_widget("StudioEdit")
        if not edit or edit.get("value", "").strip() != source.strip():
            client.type("StudioEdit", source)
            time.sleep(0.2)
        client.click("StudioRun")
        time.sleep(0.3)   # allow SatCom to appear if the script triggers it
        client.dismiss_satcom()  # close briefing that some robots open on script start
        client.click("StudioOK")
        client.set_speed(16.0)   # Studio resets simulation speed to 1× on open

        win_screen = await_win(client, win_timeout, label)
        assert win_screen == "LevelComplete", \
            f"{label}: unexpected win screen '{win_screen}'"
        assert client.find_widget("ButtonEndLevel") is not None, \
            f"{label}: LevelComplete reached but ButtonEndLevel missing"
    finally:
        teardown_to_main_menu(client)
