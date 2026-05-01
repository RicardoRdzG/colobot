"""HTTP client wrapper for the Colobot agent server."""

import base64
import math
import time
from pathlib import Path
from typing import Callable, Optional

import requests

SNAPSHOTS_DIR = Path(__file__).parent / "snapshots"


class AgentClient:
    def __init__(self, url: str = "http://127.0.0.1:7777"):
        self.url = url.rstrip("/")
        self._session = requests.Session()

    # ------------------------------------------------------------------
    # Core API
    # ------------------------------------------------------------------

    def health(self) -> dict:
        return self._get("/health")

    def state(self) -> dict:
        return self._get("/state")

    def click(self, widget_id: str) -> dict:
        return self._post("/click", {"id": widget_id})

    def type(self, widget_id: str, text: str) -> dict:
        return self._post("/type", {"id": widget_id, "text": text})

    def select(self, widget_id: str, *, item: str = None, index: int = None) -> dict:
        body = {"id": widget_id}
        if item is not None:
            body["item"] = item
        elif index is not None:
            body["index"] = index
        return self._post("/select", body)

    def key(self, key: str, action: str = "tap") -> dict:
        """Send a key event. action='tap' (default), 'down', or 'up'.
        Use 'down'/'up' to hold a key for continuous input (e.g. walking
        the astronaut). 'tap' sends down+up immediately.
        """
        body = {"key": key}
        if action != "tap":
            body["action"] = action
        return self._post("/key", body)

    def key_down(self, key: str) -> dict:
        """Press and hold a key. Must be paired with key_up later."""
        return self.key(key, "down")

    def key_up(self, key: str) -> dict:
        """Release a held key."""
        return self.key(key, "up")

    def key_hold(self, key: str, duration: float) -> dict:
        """Press a key, sleep for duration seconds, then release."""
        self.key_down(key)
        time.sleep(duration)
        return self.key_up(key)

    # ------------------------------------------------------------------
    # Astronaut (Me) walking — uses held arrow keys
    # ------------------------------------------------------------------
    #
    # Heading convention: rotation.y in objects() is the heading angle
    # in radians. Forward direction at heading h is (cos h, -sin h) in
    # the (x, z) plane. The Right arrow increases heading; Left decreases.
    # See test_10's ch3/lvl1 prep hook for the canonical usage example.

    def walk_me_to(self, tx: float, tz: float, *,
                   tolerance: float = 1.5,
                   max_iters: int = 40) -> bool:
        """Walk the currently-selected astronaut Me to (tx, tz) in world coords.
        Issues short Up / Left / Right key holds. Returns True on success."""
        for _ in range(max_iters):
            objs = self.objects()
            me = next((o for o in objs if o["type"] == "Me"), None)
            if me is None:
                return False
            x, z = me["pos"]["x"], me["pos"]["z"]
            dx, dz = tx - x, tz - z
            dist = math.hypot(dx, dz)
            if dist < tolerance:
                return True
            target_ang = math.atan2(-dz, dx)        # forward = (cos h, -sin h)
            cur_ang    = me["rotation"]["y"]
            diff = (target_ang - cur_ang + math.pi) % (2 * math.pi) - math.pi
            if abs(diff) > 0.2:
                turn_key = "Right" if diff > 0 else "Left"
                self.key_hold(turn_key, min(abs(diff) * 0.18, 0.4))
            else:
                self.key_hold("Up", min(dist * 0.04, 0.4))
            time.sleep(0.1)
        return False

    def me_pickup_and_install(self, item_type: str, robot_type: str) -> bool:
        """Walk Me to the nearest item of `item_type`, take it, walk to the
        rear of the named robot, and drop it on the robot's power slot.

        Returns True on success. Used to prepare levels where the trainer
        bot starts without a power cell (power=-1) and the astronaut must
        manually install one before any robot script can run.
        """
        # key_hold durations are real-time; reset to 1× for predictable movement
        # regardless of whatever simulation speed is active, then restore.
        try:
            _saved_speed = self.get_speed()
            if _saved_speed != 1.0:
                self.set_speed(1.0)
        except Exception:
            _saved_speed = 1.0

        try:
            # Select Me — its shortcut is the first event id in the 1500 range.
            # In all level layouts seen so far this is evt:1501.
            for evt in range(1501, 1510):
                if self.find_widget(f"evt:{evt}") is not None:
                    self.click(f"evt:{evt}")
                    time.sleep(0.4)
                    if self.find_widget("ButtonTake") is not None:
                        break
            else:
                return False

            objs = self.objects()
            item = next((o for o in objs if o["type"] == item_type), None)
            bot  = next((o for o in objs if o["type"] == robot_type), None)
            if item is None or bot is None:
                return False

            # Step 1 — walk Me to the item and pick it up. Tolerance is generous
            # because turn-step convergence rarely lands exactly on target; 2.5
            # units is comfortably within ButtonTake's interaction range.
            self.walk_me_to(item["pos"]["x"], item["pos"]["z"], tolerance=2.5, max_iters=60)
            self.click("ButtonTake")
            time.sleep(0.6)

            # Step 2 — compute a position behind the bot (along its -forward axis)
            bh = bot["rotation"]["y"]
            rear_x = bot["pos"]["x"] - math.cos(bh) * 5.0
            rear_z = bot["pos"]["z"] + math.sin(bh) * 5.0

            # Step 3 — walk to the rear and drop
            self.walk_me_to(rear_x, rear_z, tolerance=2.5, max_iters=60)
            self.click("ButtonTake")
            time.sleep(0.6)

            # Verify the cell is now attached (its world position becomes a small
            # local offset relative to the bot, e.g. (1.7, -0.5, 1.1)).
            objs = self.objects()
            item = next((o for o in objs if o["type"] == item_type), None)
            if item is None:
                return False
            return abs(item["pos"]["x"]) < 10 and abs(item["pos"]["z"]) < 10
        finally:
            try:
                if _saved_speed != 1.0:
                    self.set_speed(_saved_speed)
            except Exception:
                pass

    def click_pos(self, x: float, y: float) -> dict:
        """Click at interface coordinates (x, y) in [0,1], origin bottom-left."""
        return self._post("/click_pos", {"x": x, "y": y})

    def objects(self) -> list:
        """Return a list of game objects with their projected screen positions."""
        return self._get("/objects")

    def find_object(self, type_name: str) -> Optional[dict]:
        """Return the first visible on-screen object with the given type name, or None."""
        return next(
            (o for o in self.objects() if o["type"] == type_name and o["visible"]),
            None,
        )

    # ------------------------------------------------------------------
    # Raw mouse input
    # ------------------------------------------------------------------

    def mouse_move(self, x: float, y: float) -> dict:
        """Move cursor to interface coords (x, y) in [0..1] without clicking."""
        return self._post("/mouse_move", {"x": x, "y": y})

    def drag(self, from_x: float, from_y: float,
             to_x: float, to_y: float,
             steps: int = 20, button: str = "left") -> dict:
        """Click-and-drag from (from_x, from_y) to (to_x, to_y) in interface coords."""
        return self._post("/drag", {
            "from_x": from_x, "from_y": from_y,
            "to_x":   to_x,   "to_y":   to_y,
            "steps":  steps,  "button": button,
        })

    def mouse_down(self, x: float, y: float, button: str = "left") -> dict:
        """Press a mouse button at interface coords. Must be followed by mouse_up."""
        return self._post("/mouse_button", {"action": "down", "x": x, "y": y, "button": button})

    def mouse_up(self, x: float, y: float, button: str = "left") -> dict:
        """Release a mouse button at interface coords."""
        return self._post("/mouse_button", {"action": "up", "x": x, "y": y, "button": button})

    def window(self) -> dict:
        """Return actual window geometry: logical size, drawable size, display usable area."""
        return self._get("/window")

    def launch(self, category: str, chap: int, rank: int) -> dict:
        """Start a level directly: category is 'Exercises'|'Challenges'|'Missions'|'FreeGame'."""
        return self._post("/launch", {"category": category, "chap": chap, "rank": rank})

    def set_speed(self, speed: float) -> dict:
        """Set the simulation speed multiplier (1.0=normal, 4.0=4× faster, max 100)."""
        return self._post("/speed", {"speed": speed})

    def get_speed(self) -> float:
        """Return the current simulation speed multiplier."""
        return self._get("/speed")["speed"]

    def get_program(self, slot: int = -1) -> dict:
        """Return source code and compile state for a program slot on the selected robot."""
        params = {} if slot < 0 else {"slot": slot}
        return self._get("/program", params=params)

    def set_program(self, source: str, slot: int = -1, compile: bool = False) -> dict:
        """Write source code to a program slot. If compile=True, compile and return errors."""
        body: dict = {"source": source}
        if slot >= 0:
            body["slot"] = slot
        if compile:
            body["compile"] = True
        return self._post("/program", body)

    def compile(self, source: str, slot: int = -1) -> Optional[dict]:
        """Write and compile source. Returns error dict on failure, None on success."""
        result = self.set_program(source, slot=slot, compile=True)
        return result.get("error")

    def diagnostics(self, slot: int = -1) -> dict:
        """Return compile and runtime state for the selected robot's program slot."""
        params = {} if slot < 0 else {"slot": slot}
        return self._get("/diagnostics", params=params)

    def run_program(self) -> None:
        """Click ButtonRunProgram on the selected robot's HUD."""
        self.click("ButtonRunProgram")

    def dismiss_satcom(self, delay: float = 0.3) -> bool:
        """Close the SatCom overlay if it is currently showing.
        Returns True if SatCom was present and dismissed."""
        if self.find_widget("SatComClose") is None:
            return False
        try:
            self.click("SatComClose")
            time.sleep(delay)
        except Exception:
            pass
        return True

    def open_studio(
        self,
        expected_source: str = None,
        robot_filter: Optional[Callable[["AgentClient"], bool]] = None,
    ) -> bool:
        """Select a programmable robot and open its Studio code editor.

        Prefers robots that already have a slot matching expected_source (runnable
        slots first, non-runnable accepted) to avoid injecting onto the wrong robot.
        If no robot has the source, falls back to the first robot with an existing
        runnable slot (anti-pollution) or, as a last resort, adds a new slot.
        Pass robot_filter to select by source content rather than exact text match
        (e.g. to target a robot currently running a specific script).
        Returns True on success, False if no suitable robot found.
        """
        # Single-pass scan: record the best match and a fallback simultaneously.
        match_evt  = None   # evt id of the target robot (source match / filter match)
        match_slot = None   # slot index to pre-select on match_evt (None = keep current)
        # Two-tier fallback: idle robots (ButtonRunProgram visible) are preferred over
        # robots currently running a program.  Levels with a student-controlled robot and
        # an auto-starting slave bot (run=1) would otherwise inject the solution onto the
        # slave (which appears first in the shortcut list).
        fallback_evt  = None   # tier-1: not currently running
        fallback_slot = None
        fallback_busy_evt  = None  # tier-2: currently running (last resort)
        fallback_busy_slot = None
        last_clicked_evt  = None   # last robot the scan actually clicked

        for evt_id in range(1501, 1550):
            widget_id = f"evt:{evt_id}"
            if self.find_widget(widget_id) is None:
                continue
            try:
                self._post("/click", {"id": widget_id})
            except Exception:
                continue
            last_clicked_evt = widget_id
            time.sleep(0.3)
            # Some robots open SatCom when selected; close it so the toolbar appears.
            self.dismiss_satcom(delay=0.5)
            if self.find_widget("ButtonOpenStudio") is None:
                continue

            # robot_filter: caller supplies a predicate; source matching is skipped.
            if robot_filter is not None:
                try:
                    if robot_filter(self):
                        match_evt, match_slot = widget_id, None
                        break
                except Exception:
                    pass
                continue  # filter rejected this robot; keep scanning

            # Scan program slots for a source match.
            # Also record any runnable slot as an anti-pollution fallback so we
            # never add a new slot when one already exists on the right robot.
            reuse_slot = None
            any_runnable = None
            if expected_source is not None:
                try:
                    first = self.get_program(0)
                    slot_count = first.get("slot_count", 1)
                    slot_list = [first] + [self.get_program(s) for s in range(1, slot_count)]
                    for s, p in enumerate(slot_list):
                        is_run = p.get("runnable", False)
                        src_ok = p.get("source", "").strip() == expected_source.strip()
                        if src_ok and is_run:
                            reuse_slot = s   # exact runnable match — best possible
                            break
                        if src_ok and reuse_slot is None:
                            reuse_slot = s   # non-runnable source match
                        if is_run and any_runnable is None:
                            any_runnable = s  # any runnable slot, for anti-pollution
                except Exception:
                    pass

                if reuse_slot is not None:
                    # Prefer a runnable slot so Studio's Run button is enabled.
                    # If the only source match is the scene's non-runnable solution
                    # slot, use any other runnable slot (content will be re-injected).
                    # If no runnable slot exists at all, skip match and let the
                    # fallback path add a new slot via ButtonAddProgram instead.
                    is_runnable_match = slot_list[reuse_slot].get("runnable", False)
                    usable_slot = reuse_slot if is_runnable_match else any_runnable
                    if usable_slot is not None:
                        match_evt, match_slot = widget_id, usable_slot
                        break   # right robot with usable slot found — stop scanning

            # No source match on this robot; record as fallback if it can take a slot.
            # Detect "currently running" via ButtonAddProgram's enabled state:
            # bProgEnable = !IsProgram() disables ButtonAddProgram while a script runs.
            # ButtonStopProgram (EVENT_OBJECT_PROGSTOP) is never created as a toolbar
            # widget, so checking for its presence is unreliable.
            add_prog = self.find_widget("ButtonAddProgram")
            if add_prog is not None:
                currently_running = not add_prog.get("enabled", True)
                if not currently_running and fallback_evt is None:
                    fallback_evt  = widget_id
                    fallback_slot = any_runnable
                elif currently_running and fallback_busy_evt is None:
                    fallback_busy_evt  = widget_id
                    fallback_busy_slot = any_runnable

        # Use tier-2 only when no idle robot was found.
        if fallback_evt is None:
            fallback_evt  = fallback_busy_evt
            fallback_slot = fallback_busy_slot

        use_evt  = match_evt  if match_evt  is not None else fallback_evt
        use_slot = match_slot if match_evt  is not None else fallback_slot

        if use_evt is None:
            return False

        # Re-select the target robot only when the scan ended on a different one.
        # If the scan already selected use_evt (match found → broke out of the loop),
        # a second click on the same shortcut button would toggle the selection away
        # to whatever was selected before — opening Studio on the wrong robot.
        if use_evt != last_clicked_evt:
            try:
                self._post("/click", {"id": use_evt})
                time.sleep(0.3)
            except Exception:
                return False
        self.dismiss_satcom(delay=0.5)

        if self.find_widget("ButtonOpenStudio") is None:
            return False

        if use_slot is not None:
            # Pre-select the identified slot so Studio opens on it.
            # The correct robot is already selected (either from the scan break, or
            # from the explicit re-click above). select("ListPrograms") updates only
            # the selected robot's m_selScript — it does not switch robots.
            try:
                self.select("ListPrograms", index=use_slot)
                time.sleep(0.2)
            except Exception:
                pass
        elif robot_filter is None and self.find_widget("ButtonAddProgram") is not None:
            # Add a new slot only when no reusable slot exists and no filter is in use.
            try:
                self.click("ButtonAddProgram")
                time.sleep(0.2)
            except Exception:
                return False

        try:
            self.click("ButtonOpenStudio")
            self.wait_for_screen("Studio", timeout=5.0)
            return True
        except Exception:
            return False

    def select_programmable_robot(self, timeout: float = 3.0) -> bool:
        """
        Click through shortcut buttons (1501-1549) until ButtonOpenStudio appears,
        indicating a programmable robot is selected.
        Returns True if a programmable robot was selected, False otherwise.
        """
        for evt_id in range(1501, 1550):
            widget_id = f"evt:{evt_id}"
            if self.find_widget(widget_id) is None:
                continue
            try:
                self._post("/click", {"id": widget_id})
            except Exception:
                continue
            time.sleep(0.3)
            if self.find_widget("ButtonOpenStudio") is not None:
                return True
        return False

    def console(self, command: str) -> None:
        """Open the in-game console, execute a command, and close it."""
        self.key("Backquote")
        time.sleep(0.1)
        self.type("EditConsole", command)
        self.key("Return")

    def screenshot(self, source: str = "gl") -> bytes:
        """Returns raw PNG bytes. source='gl' (default) or 'os'."""
        params = {} if source == "gl" else {"source": source}
        data = self._get("/screenshot", params=params)
        return base64.b64decode(data["png"])

    # ------------------------------------------------------------------
    # Convenience helpers
    # ------------------------------------------------------------------

    def wait_for_screen(self, screen: str, timeout: float = 15.0, poll: float = 0.5) -> dict:
        """Poll /state until the named screen is active. Returns the state dict."""
        deadline = time.monotonic() + timeout
        last_screen = None
        while time.monotonic() < deadline:
            try:
                s = self.state()
                last_screen = s["screen"]
                if last_screen == screen:
                    return s
            except Exception:
                pass
            time.sleep(poll)
        raise TimeoutError(
            f"Screen '{screen}' not reached within {timeout}s (last: '{last_screen}')"
        )

    def widget_ids(self, state: Optional[dict] = None) -> list[str]:
        """Return the list of widget IDs from the current (or given) state."""
        s = state or self.state()
        return [w["id"] for w in s["widgets"]]

    def find_widget(self, widget_id: str, state: Optional[dict] = None) -> Optional[dict]:
        s = state or self.state()
        return next((w for w in s["widgets"] if w["id"] == widget_id), None)

    # ------------------------------------------------------------------
    # Visual regression
    # ------------------------------------------------------------------

    def save_snapshot(self, name: str, source: str = "gl") -> Path:
        """Capture and save a baseline screenshot. Returns the saved path."""
        SNAPSHOTS_DIR.mkdir(exist_ok=True)
        path = SNAPSHOTS_DIR / f"{name}.png"
        path.write_bytes(self.screenshot(source=source))
        return path

    def assert_snapshot(self, name: str, source: str = "gl", tolerance: float = 0.02,
                        update: bool = False):
        """
        Compare a screenshot against the saved baseline.
        tolerance: max allowed fraction of pixels that differ (default 2%).
        Saves the actual screenshot next to the baseline as <name>.actual.png on failure.
        If no baseline exists, saves one and passes (first-run behaviour).
        """
        try:
            from PIL import Image, ImageChops
        except ImportError:
            raise ImportError("pillow is required for visual regression: pip install pillow")

        import io

        baseline_path = SNAPSHOTS_DIR / f"{name}.png"
        actual_bytes = self.screenshot(source=source)

        update = update or getattr(self, "_update_snapshots", False)
        if update or not baseline_path.exists():
            baseline_path.write_bytes(actual_bytes)
            return  # First run or explicit update: save baseline, don't fail

        baseline = Image.open(baseline_path).convert("RGB")
        actual = Image.open(io.BytesIO(actual_bytes)).convert("RGB")

        if baseline.size != actual.size:
            actual_path = SNAPSHOTS_DIR / f"{name}.actual.png"
            actual_path.write_bytes(actual_bytes)
            raise AssertionError(
                f"Snapshot '{name}': size mismatch {baseline.size} vs {actual.size}"
            )

        diff = ImageChops.difference(baseline, actual)
        total = baseline.width * baseline.height
        differing = sum(1 for px in diff.getdata() if any(c > 10 for c in px))
        ratio = differing / total

        if ratio > tolerance:
            actual_path = SNAPSHOTS_DIR / f"{name}.actual.png"
            actual_path.write_bytes(actual_bytes)
            raise AssertionError(
                f"Snapshot '{name}': {ratio:.1%} pixels differ (tolerance {tolerance:.1%}). "
                f"Actual saved to {actual_path}. "
                f"Run with --update-snapshots to accept the new baseline."
            )

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _get(self, path: str, params: dict = None) -> dict:
        r = self._session.get(self.url + path, params=params, timeout=10)
        r.raise_for_status()
        body = r.json()
        if not body["ok"]:
            raise RuntimeError(f"Agent server error on GET {path}: {body['error']}")
        return body["data"]

    def _post(self, path: str, payload: dict) -> dict:
        r = self._session.post(self.url + path, json=payload, timeout=10)
        r.raise_for_status()
        body = r.json()
        if not body["ok"]:
            raise RuntimeError(f"Agent server error on POST {path}: {body['error']}")
        return body["data"]
