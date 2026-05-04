# Agent Command Server — Spec

**Status:** DRAFT v0.13 — CBot string-char indexing implemented; both previously-disabled CBot unit tests enabled; 206/206 passing  
**Approach:** spec-first; analyst updates this doc as prototype clarifies unknowns  
**Agents:**
- `analyst` — owns this spec; refines it as prototype runs
- `coder` — implements strictly from the finalized spec
- `verifier` — checks implementation against spec; writes test results
- `human` — validates on macOS and Linux; signs off each milestone

---

## 1. Goal

Enable an AI agent to interact with a running Colobot instance without relying on
OS-specific accessibility APIs or coordinate-based mouse automation.  
The server must work identically on macOS, Linux, and Windows.

### AI agent validation loop

The primary use case is an AI agent that:

1. Starts a level (`/launch`)
2. Selects a robot and injects source code (`POST /program`)
3. Compiles and reads errors (`GET /diagnostics`)
4. Runs the program (`POST /click ButtonRunProgram`)
5. Polls object state to verify robot behaviour (`GET /objects`)
6. Detects win/loss from the screen name (`GET /state`)
7. Takes a screenshot to record evidence (`GET /screenshot`)

This loop requires no OS-level accessibility APIs and runs identically on macOS,
Linux (headless with Xvfb), and Windows.

### Non-goals (v1)

- Authentication / multi-client support
- Replaying recorded sessions
- Modifying game data (levels, save files)
- Production use — dev/test mode only

---

## 2. Activation

The server is **opt-in** and never runs in normal gameplay.

```
./colobot -agentserver [port]
```

- Default port: `7777`
- Binds to `127.0.0.1` only (never exposed to network)
- Server starts before the main window appears
- Process exits normally when Colobot quits; server tears down cleanly

**Note:** `-headless` skips the SDL window, removing the GL context the agent server
needs for rendering and screenshots. When `-agentserver` is present the engine
always creates a window (hidden if `-headless` is also set). For CI use without a
physical display, start Xvfb first and omit `-headless`; the test fixtures handle
this automatically.

---

## 3. Architecture

```
AI Agent / test script
        │  HTTP JSON  (localhost:7777)
        ▼
┌──────────────────────────────────────┐
│  CAgentServer  (new class)           │
│  - thin HTTP server on a thread      │
│  - queues commands into main thread  │
│  - returns results synchronously     │
└───────────────┬──────────────────────┘
                │  command queue + result future
                ▼
┌──────────────────────────────────────┐
│  Colobot main thread                 │
│  - executes UI actions each frame    │
│  - reads widget tree from CInterface │
│  - captures GL framebuffer pre-swap  │
└──────────────────────────────────────┘
```

**Threading model:** The HTTP server runs on a dedicated thread. Commands are
posted to a `std::queue` protected by a mutex. The main thread drains the queue
once per frame and resolves the associated `std::promise`. The HTTP thread blocks
on the future until the main thread fulfills it (timeout: 5 s).

**Screenshot threading:** Screenshot requests use a separate atomic flag +
promise. `CApplication::Render()` calls `CaptureFrameIfPending()` between
`m_engine->Render()` and `SDL_GL_SwapWindow()`, when FBO 0 holds the final
composited frame. This avoids undefined backbuffer contents after swap.

---

## 3.1 Coordinate Systems

The agent API uses a single unified coordinate system for all mouse/click operations.
Understanding the full stack matters when diagnosing coordinate-mapping bugs.

```
┌──────────────────────────────────────────────────────┐
│  Interface space  [0..1] × [0..1]                    │  ← what all agent endpoints use
│  Origin: bottom-left. Y increases upward.            │
│  Used by: /click_pos, /mouse_move, /drag             │
└─────────────────┬────────────────────────────────────┘
                  │  ×winSize.x / (1−y)×winSize.y  (y-flip)
┌─────────────────▼────────────────────────────────────┐
│  SDL logical pixels  (0,0) top-left                  │  ← SDL_GetWindowSize()
│  What SDL events report and receive                  │
│  On non-HiDPI displays = drawable pixels             │
└─────────────────┬────────────────────────────────────┘
                  │  ×(drawable/logical) scale factor
┌─────────────────▼────────────────────────────────────┐
│  SDL drawable pixels  (GL resolution)                │  ← SDL_GL_GetDrawableSize()
│  What glReadPixels captures; 2× on HiDPI (Retina)   │
└─────────────────┬────────────────────────────────────┘
                  │  window position on desktop
┌─────────────────▼────────────────────────────────────┐
│  OS screen coordinates                               │  ← SDL_GetWindowPosition()
│  Affected by: title bar, dock/taskbar, multi-monitor │
│  Usable area ≠ full display area                     │
└──────────────────────────────────────────────────────┘
```

### The coordinate-mapping bug class

When the user selects maximum display resolution in SetupDisplay, the game sets the
SDL window to that size. If the OS reserves space for a dock or taskbar, the actual
usable content area is smaller. If the game does not re-query the window size after
this change, internal `[0..1]` coordinates will be mapped to the wrong pixel offsets.

**How to test with the agent:**

1. `GET /window` — capture the baseline: `logical_w/h`, `drawable_w/h`,
   `display_usable` (OS usable area minus dock/taskbar).
2. Switch resolution via the SetupDisplay screen (`/click`, `/select`).
3. `GET /window` again — compare. If `logical_w/h` does not match what the game
   selected, the window state was not updated.
4. `POST /mouse_move {"x":0.5,"y":0.0}` — move to the bottom edge.
5. `POST /drag` the Studio title bar to a position near the edge, then verify with
   `/screenshot` that the window moved as expected.

---

## 4. Protocol

- **Transport:** HTTP/1.1 over TCP, `localhost:7777`
- **Format:** JSON request body + JSON response body
- **Encoding:** UTF-8
- All requests use `Content-Type: application/json`
- All responses include `Content-Type: application/json`

### Response envelope

```json
{ "ok": true,  "data": { ... } }
{ "ok": false, "error": "human-readable description" }
```

HTTP status codes mirror `ok`: `200` on success, `4xx`/`5xx` on error.

---

## 5. API Reference

### 5.1 `GET /state`

Returns the current UI state.

**Response `data`:**

```json
{
  "screen": "PlayerSelect",
  "widgets": [
    { "id": "ButtonOK",       "type": "button", "label": "OK",     "enabled": true },
    { "id": "EditPlayerName", "type": "edit",   "value": "Player", "enabled": true },
    { "id": "ListPlayers",    "type": "list",   "items": ["Claude"], "selected": 0 }
  ]
}
```

Widget types: `button`, `edit`, `list`, `label`, `slider`, `check`.

**Known screens** (detected by `DetectScreen()` from widget presence):

| Screen name | Detection condition |
|-------------|---------------------|
| `PlayerSelect` | `EditPlayerName` + `ListPlayers` present |
| `MainMenu` | `ButtonExercises` + `ButtonQuit` present |
| `LevelSelect` | `ListChapter` + `ListLevel` present |
| `LevelComplete` | `ButtonEndLevel` present |
| `SetupGame` | `ListLanguage` present |
| `SetupDisplay` | `ButtonTabDisplay` + `ButtonTabGraphics` present (no `ListLanguage`) |
| `Studio` | `StudioEdit` + `StudioRun` present |
| `SatCom` | `SatComContent` + `SatComClose` present |
| `InGameMenu` | `ButtonAbort` or `ButtonAgain` present |
| `InGame` | ≥7 widgets, none of the above match |
| `unknown` | fewer than 7 widgets (loading / transition) |

### 5.2 `POST /click`

Clicks a widget by its `id`. Fails `404` if not found/visible; `409` if disabled.

```json
{ "id": "ButtonOK" }
```

### 5.3 `POST /select`

Selects a list item by value or index.

```json
{ "id": "ListPlayers", "item": "Claude" }
{ "id": "ListPlayers", "index": 0 }
```

### 5.4 `POST /type`

Sets edit widget value directly (bypasses OS input).

```json
{ "id": "EditPlayerName", "text": "Claude" }
```

### 5.5 `POST /key`

Sends a named key. Supported: `F1`–`F12`, `Escape`, `Return`, `Space`, `Tab`, `Backquote`.

```json
{ "key": "Escape" }
```

### 5.6 `GET /screenshot`

Returns a PNG of the current frame.

**Query parameters:**

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `source`  | `gl`, `os` | `gl` | Capture method (see below) |

**`source=gl` (default):** GL framebuffer readback via `glReadPixels` on FBO 0,
captured pre-swap. No external tools. Works headless (with Xvfb). Captures exactly
what the renderer produced. Use for CI, regression tests, game-logic validation.

**`source=os`:** OS screencapture tool (`screencapture` on macOS,
`import -window root` / `scrot` on Linux). Captures what appeared on screen
including window chrome, compositor scaling, HiDPI effects. Requires a visible
window and display server. Use for platform-specific visual validation.

**Response `data`:**
```json
{ "png": "<base64-encoded PNG>" }
```

### 5.7 `GET /health`

Liveness check. Returns immediately without touching the main thread.

```json
{ "status": "ok", "version": "0.3.0-alpha" }
```

### 5.8 `GET /objects`

Returns all active game objects with their projected screen-space positions.

**Response `data`:** array of objects:

```json
[
  {
    "id": 42,
    "type": "WheeledShooter",
    "screen_x": 0.50,
    "screen_y": 0.53,
    "visible": true,
    "pos": { "x": 0.0, "y": 0.0, "z": 0.0 }
  }
]
```

- `screen_x`, `screen_y` — interface coords `[0..1]`, origin bottom-left; `-1` if behind camera
- `visible` — true when `screen_x`/`screen_y` are both in `[0..1]`
- `type` — CBot object type name (e.g. `"WheeledShooter"`, `"AlienSpider"`)
- `pos` — world-space position

**Implementation:** `CEngine::WorldToInterface()` projects world positions through
`m_matView` + `m_matProj`. Only objects with a valid `GetObjectName()` are included.

Additional fields are included when the object supports the relevant interface:

```json
{
  "energy": 0.85,
  "shield": 1.0,
  "rotation": { "x": 0.0, "y": 1.57, "z": 0.0 },
  "program_running": true,
  "task": "TaskGoto"
}
```

- `energy` — power cell charge `[0..1]`; omitted if no power cell slot
- `shield` — shield level `[0..1]`; omitted for non-shielded objects
- `rotation` — Euler angles in radians
- `program_running` — `true` while a CBOT program is executing; omitted for non-programmable objects
- `task` — current foreground task class name (e.g. `"TaskGoto"`, `"TaskManip"`); omitted when idle

### 5.9 `GET /program`

Returns the source code and compile state of a program slot on the selected robot.

**Query parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `slot` | int | last slot | 0-based program slot index |

**Response `data`:**

```json
{
  "slot": 0,
  "slot_count": 1,
  "source": "extern void object() {\n  move(5);\n}\n",
  "compiled": false,
  "runnable": true,
  "filename": ""
}
```

Returns `404` if no robot is selected or the slot index is out of range.

### 5.10 `POST /program`

Sets the source code of a program slot and optionally compiles it.

**Request:**

```json
{
  "slot": 0,
  "source": "extern void object() {\n  move(5);\n}\n",
  "compile": true
}
```

- `slot` — defaults to the last slot if omitted
- `compile` — if `true`, compiles immediately and returns errors (default `false`)

**Response `data`:**

```json
{ "compiled": true, "error": null }
```

On compile error:

```json
{
  "compiled": false,
  "error": {
    "message": "Missing ;",
    "cursor_start": 42,
    "cursor_end": 43
  }
}
```

Returns `404` if no robot is selected or the slot is out of range.
`cursor_start` / `cursor_end` are byte offsets into `source`.

### 5.11 `GET /diagnostics`

Returns the compile and runtime state of the selected robot's selected program slot.

**Query parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `slot` | int | last slot | 0-based program slot index |

**Response `data`:**

```json
{
  "selected_robot": true,
  "slot": 0,
  "compiled": true,
  "running": false,
  "error": null
}
```

When an error exists (compile or runtime):

```json
{
  "selected_robot": true,
  "slot": 0,
  "compiled": false,
  "running": false,
  "error": {
    "message": "Missing ;",
    "cursor_start": 42,
    "cursor_end": 43
  }
}
```

- `running` — `true` while a program is executing on the selected robot
- `error` — `null` when compiled OK and no runtime error occurred; populated after a
  failed compile or a runtime crash

Returns `404` if no robot is selected.

### 5.12 `POST /launch`

Starts a level directly without navigating menus. Calls `CRobotMain::SetLevel()` +
`ChangePhase(PHASE_SIMUL)` on the main thread; the screen transitions to `InGame`
asynchronously (poll `/state` until `screen == "InGame"`).

**Request:**

```json
{ "category": "Exercises", "chap": 1, "rank": 1 }
```

- `category` — one of `"Exercises"`, `"Challenges"`, `"Missions"`, `"FreeGame"`, `"CodeBattles"`
- `chap` — chapter number (1-based)
- `rank` — level number within chapter (1-based)

**Response `data`:**

```json
{ "category": "Exercises", "chap": 1, "rank": 1 }
```

Returns `400` for an unknown category name.

**Note:** The level must exist on disk; a missing level will trigger the in-game error screen.

---

### 5.13 `POST /mouse_move`

Moves the mouse cursor to an interface-space position **without clicking**.
Use this to trigger hover/highlight states, reveal tooltips, or position the cursor
before a subsequent `mouse_button` call.

```json
{ "x": 0.5, "y": 0.5 }
```

Pushes a single `SDL_MOUSEMOTION` event. `x` and `y` are interface coords `[0..1]`
(bottom-left origin). Returns the resolved pixel position as confirmation:

```json
{ "x": 0.5, "y": 0.5, "px": 320, "py": 240 }
```

---

### 5.14 `POST /drag`

Simulates a click-and-drag gesture: `MOUSEBUTTONDOWN` at `from`, then a sequence of
`MOUSEMOTION` events along the straight-line path, then `MOUSEBUTTONUP` at `to`.

```json
{
  "from_x": 0.3,  "from_y": 0.8,
  "to_x":   0.6,  "to_y":   0.5,
  "steps":  20,
  "button": "left"
}
```

| Field | Default | Description |
|-------|---------|-------------|
| `from_x`, `from_y` | required | start position in interface coords `[0..1]` |
| `to_x`, `to_y` | required | end position in interface coords `[0..1]` |
| `steps` | `20` | number of intermediate `MOUSEMOTION` events (minimum 1) |
| `button` | `"left"` | `"left"`, `"right"`, or `"middle"` |

**Use cases:**
- Dragging the Script Studio window (title bar drag)
- Dragging a robot or camera in the viewport
- Testing that the game correctly tracks mouse position during a window resize/reposition

**Response `data`:**

```json
{ "from": {"x":0.3,"y":0.8,"px":192,"py":96}, "to": {"x":0.6,"y":0.5,"px":384,"py":240}, "steps":20 }
```

---

### 5.15 `POST /mouse_button`

Raw independent mouse button press or release. Use this for multi-step gestures that
`/drag` cannot express (e.g. hold button, move via `/mouse_move` multiple times, release).

```json
{ "action": "down", "x": 0.3, "y": 0.8, "button": "left" }
{ "action": "up",   "x": 0.6, "y": 0.5, "button": "left" }
```

| Field | Values | Default | Description |
|-------|--------|---------|-------------|
| `action` | `"down"`, `"up"` | required | press or release |
| `x`, `y` | `[0..1]` | required | interface coords |
| `button` | `"left"`, `"right"`, `"middle"` | `"left"` | which button |

**Note:** The caller is responsible for always following every `down` with an `up`.
Leaving a button held will put the game into a stuck drag state.

---

### 5.16 `GET /window`

Returns the actual window geometry as seen by the OS and SDL. Use this to diagnose
coordinate-mapping bugs caused by resolution changes, dock/taskbar intrusion, or
HiDPI scaling mismatches.

**Response `data`:**

```json
{
  "x": 0, "y": 23,
  "logical_w": 1440, "logical_h": 877,
  "drawable_w": 2880, "drawable_h": 1754,
  "scale_x": 2.0, "scale_y": 2.0,
  "display_index": 0,
  "display_bounds":  { "x": 0, "y": 0, "w": 1440, "h": 900 },
  "display_usable":  { "x": 0, "y": 23, "w": 1440, "h": 877 },
  "flags": ["shown", "resizable", "input_focus"]
}
```

| Field | Source | Description |
|-------|--------|-------------|
| `x`, `y` | `SDL_GetWindowPosition` | Window top-left in OS screen coords |
| `logical_w/h` | `SDL_GetWindowSize` | SDL logical size (what game thinks it has) |
| `drawable_w/h` | `SDL_GL_GetDrawableSize` | Physical framebuffer pixels (2× on Retina) |
| `scale_x/y` | `drawable / logical` | HiDPI scale factor |
| `display_index` | `SDL_GetWindowDisplayIndex` | Which monitor the window is on |
| `display_bounds` | `SDL_GetDisplayBounds` | Full monitor area |
| `display_usable` | `SDL_GetDisplayUsableBounds` | Monitor area minus OS dock/taskbar |
| `flags` | `SDL_GetWindowFlags` | Active SDL window flags as strings |

**Diagnosing the max-resolution mapping bug:**

If the user selects their full display resolution in SetupDisplay, `logical_w/h`
should equal `display_usable.w/h`. If it equals `display_bounds.w/h` instead, the
game set the window to the full display size without accounting for the dock/taskbar —
clicks in the bottom strip will be clipped or offset.

---

## 6. Widget Registry

Controls are identified by stable string IDs registered in `agent_server.cpp`
(`GetRegistry()`). Unregistered controls fall back to `"evt:N"`.

### 6.1 Navigation screens

| Screen | Widget ID | Type | Notes |
|--------|-----------|------|-------|
| PlayerSelect | `EditPlayerName` | edit | player name field |
| PlayerSelect | `ListPlayers` | list | saved profiles |
| PlayerSelect | `ButtonOK` | button | confirm / enter game |
| PlayerSelect | `ButtonDelete` | button | delete profile |
| PlayerSelect | `LabelPlayerName` | label | |
| PlayerSelect | `ButtonCustomize` | button | open avatar customizer |
| MainMenu | `ButtonExercises` | button | |
| MainMenu | `ButtonChallenges` | button | |
| MainMenu | `ButtonMissions` | button | |
| MainMenu | `ButtonFreeGame` | button | |
| MainMenu | `ButtonCodeBattles` | button | |
| MainMenu | `ButtonPlus` | button | extra content |
| MainMenu | `ButtonMods` | button | mod manager |
| MainMenu | `ButtonUserLevels` | button | community levels |
| MainMenu | `ButtonPlayerName` | button | switch profile |
| MainMenu | `ButtonSetup` | button | settings |
| MainMenu | `ButtonQuit` | button | |
| MainMenu | `ButtonSatCom` | button | open documentation |
| LevelSelect | `ListChapter` | list | chapter list |
| LevelSelect | `ListLevel` | list | level list |
| LevelSelect | `ButtonPlay` | button | start level |
| LevelSelect | `ButtonBack` | button | |
| LevelSelect | `ButtonResume` | button | resume saved game |

### 6.2 In-game overlays

| Screen | Widget ID | Type | Notes |
|--------|-----------|------|-------|
| InGameMenu | `ButtonAbort` | button | quit to menu |
| InGameMenu | `ButtonContinue` | button | resume game |
| InGameMenu | `ButtonAgain` | button | restart level |
| InGameMenu | `ButtonSave` | button | |
| InGameMenu | `ButtonLoad` | button | |
| LevelComplete | `ButtonEndLevel` | button | dismiss ending screen |
| InGame | `EditConsole` | edit | cheat console (backtick toggle) |
| InGame | `SpeedControl` | button | game speed |

### 6.3 Setup screens

| Screen | Widget ID | Type | Notes |
|--------|-----------|------|-------|
| All setup | `ButtonTabDisplay` | button | display tab |
| All setup | `ButtonTabGraphics` | button | graphics tab |
| All setup | `ButtonTabGameplay` | button | gameplay tab |
| All setup | `ButtonTabControls` | button | controls tab |
| All setup | `ButtonTabSound` | button | sound tab |
| All setup | `ButtonBack` | button | |
| All setup | `ButtonApply` | button | |
| SetupDisplay | `ListDevice` | list | display adapter |
| SetupDisplay | `ListResolution` | list | resolution |
| SetupDisplay | `CheckFullscreen` | check | |
| SetupGraphics | `CheckShadowSpots` | check | |
| SetupGraphics | `CheckDirtyTextures` | check | |
| SetupGraphics | `CheckParticles` | check | |
| SetupGraphics | `SliderViewDistance` | slider | |
| SetupGraphics | `CheckPauseBlur` | check | |
| SetupGraphics | `CheckFog` | check | |
| SetupGraphics | `CheckShadowMapping` | check | |
| SetupGraphics | `CheckVSync` | check | |
| SetupGraphics | `ButtonQualityMin/Norm/Max` | button | preset quality |
| SetupGame | `ListLanguage` | list | language selector |
| SetupGame | `CheckTooltips` | check | |
| SetupGame | `CheckCinematics` | check | |
| SetupGame | `CheckEdgeScroll` | check | |
| SetupGame | `CheckInvertMouseX/Y` | check | |
| SetupGame | `CheckAutosave` | check | |
| SetupGame | `SliderAutosaveInterval` | slider | |
| SetupSound | `SliderSoundVolume` | slider | |
| SetupSound | `SliderMusicVolume` | slider | |

### 6.4 In-game HUD (robot selected)

| Widget ID | Type | Notes |
|-----------|------|-------|
| `ButtonDeselect` | button | deselect current object |
| `ButtonSatComOpen` | button | open SatCom Houston tab (evt:1040) |
| `ButtonHelp` | button | open object-specific help (evt:1401) |
| `ButtonCamera` | button | cycle camera mode |
| `ButtonTake` | button | pick up object (astronaut) |
| `ButtonMoveLeft/Right/Forward/Back` | button | manual movement |
| `ButtonJetUp/Down` | button | jet pack (flying bots) |
| `GaugeEnergy` | label | energy level indicator |
| `GaugeShield` | label | shield level indicator |
| `GaugeRange` | label | jet range indicator |
| `HudMap` | label | mini-map |
| `HudMapZoom` | button | zoom mini-map |
| `ButtonShortcutMode` | button | toggle shortcut bar mode |
| `IndicatorSaving` | label | autosave in progress |
| `ButtonBuildDerrick` | button | build derrick |
| `ButtonBuildPowerStation` | button | build power station |
| `ButtonBuildBotFactory` | button | build bot factory |
| `ButtonBuildConverter` | button | build converter |
| `ButtonBuildTower` | button | build defence tower |
| `ButtonBuildRepairCenter` | button | |
| `ButtonBuildResearchCenter` | button | |
| `ButtonBuildRadarStation` | button | |
| `ButtonBuildPowerCaptor` | button | |
| `ButtonBuildBioLab` | button | |
| `ButtonBuildNuclearPlant` | button | |
| `ButtonBuildLightningRod` | button | |
| `ButtonBuildInfoExchange` | button | |
| `ButtonBuildVault` | button | |
| `ButtonResearchTank/Fly/Thump/Cannon/Tower/Phazer/Shield/Nuclear` | button | research lab buttons |
| `ListPrograms` | list | program slots |
| `ButtonOpenStudio` | button | open code editor |
| `ButtonRunProgram` | button | run selected program |
| `ButtonAddProgram` | button | add new (runnable) program slot |
| `ButtonRemoveProgram` | button | |
| `ButtonCloneProgram` | button | |
| `ButtonProgMoveUp/Down` | button | reorder programs |
| `ButtonStopProgram` | button | stop running program |

Shortcut buttons are still addressed as `evt:1501`–`evt:1549` (one per object on map).

### 6.5 SatCom documentation viewer

Detected as screen `"SatCom"` when `SatComContent` + `SatComClose` are present.

| Widget ID | Type | Notes |
|-----------|------|-------|
| `SatComContent` | edit | full page text (read-only); used for translation verification |
| `SatComHouston` | button | Houston / mission tab |
| `SatComSat` | button | satellite / objectives tab |
| `SatComLoading` | button | loading instructions tab |
| `SatComProg` | button | programming / cbot tab |
| `SatComSoluce` | button | solution tab |
| `SatComPrev` | button | navigate back |
| `SatComNext` | button | navigate forward |
| `SatComHome` | button | go to home page |
| `SatComSize1`–`SatComSize5` | button | font size presets |
| `SatComClose` | button | close viewer (returns to InGame) |

**Translation verification:** `SatComContent.value` contains the full rendered page
text. When language is switched and the level is re-entered, this value should
differ. If it is identical to the English text, the translated file is missing.

### 6.6 Script studio

| Widget ID | Type | Notes |
|-----------|------|-------|
| `ListStudioPrograms` | list | program slots |
| `StudioEdit` | edit | source code editor |
| `StudioCompile` | button | compile only |
| `StudioRun` | button | compile + run |
| `StudioOK` | button | close and keep running |
| `StudioCancel` | button | close and stop |
| `StudioNew` | button | new program |
| `StudioOpen` | button | open from file |
| `StudioSave` | button | save to file |
| `StudioClone` | button | clone program |
| `StudioUndo` | button | |
| `StudioCut/Copy/Paste` | button | clipboard |
| `StudioFontSize` | button | font size |
| `StudioHelp` | button | open cbot help |
| `StudioRealtime` | button | toggle realtime execution |
| `StudioStep` | button | step execution |

**Important:** `StudioCompile` and `StudioRun` are disabled (`409`) when the
selected program has `runnable = false`. This is set for the pre-loaded *soluce*
(solution) program slot from the level data.

**Robot/slot selection strategy (see `open_studio()` in §7):**
- Before adding a new slot via `ButtonAddProgram`, scan existing slots with
  `GET /program?slot=N` for a source match (runnable preferred, non-runnable accepted).
  Pre-loaded soluce slots have `runnable=false` but carry the correct source — reuse
  them rather than adding a duplicate.
- Never call `ButtonAddProgram` when any runnable slot already exists on the robot
  (anti-pollution). Overwrite its source via Studio instead.
- `ListStudioPrograms` (inside Studio) is **not** for switching slots — its click
  handler opens the SatCom help viewer. Use `select("ListPrograms", index=N)` on
  the HUD *before* opening Studio to set the active slot.

---

## 7. AgentClient Python API

`tests/agent/agent_client.py` wraps the HTTP protocol.

### Core methods

| Method | HTTP | Description |
|--------|------|-------------|
| `health()` | `GET /health` | liveness check |
| `state()` | `GET /state` | current screen + widget tree |
| `click(id)` | `POST /click` | click widget by ID |
| `type(id, text)` | `POST /type` | set edit widget value |
| `select(id, index=N)` | `POST /select` | select list item |
| `key(key)` | `POST /key` | send named key |
| `click_pos(x, y)` | `POST /click_pos` | click at interface coords |
| `mouse_move(x, y)` | `POST /mouse_move` | move cursor without clicking |
| `drag(fx, fy, tx, ty, steps, button)` | `POST /drag` | click-and-drag gesture |
| `mouse_down(x, y, button)` | `POST /mouse_button` | raw button press |
| `mouse_up(x, y, button)` | `POST /mouse_button` | raw button release |
| `window()` | `GET /window` | actual window size, drawable size, display usable area |
| `objects()` | `GET /objects` | game objects with screen positions + rich state |
| `launch(category, chap, rank)` | `POST /launch` | start a level directly |
| `screenshot(source)` | `GET /screenshot` | PNG bytes |
| `get_program(slot=-1)` | `GET /program` | source + compile state for slot |
| `set_program(source, slot=-1, compile=False)` | `POST /program` | write source; optionally compile |
| `diagnostics(slot=-1)` | `GET /diagnostics` | compile + runtime state for slot |

### Convenience helpers

| Method | Description |
|--------|-------------|
| `wait_for_screen(name, timeout)` | polls `/state` until screen matches |
| `find_widget(id)` | returns widget dict or `None` |
| `widget_ids()` | list of all current widget IDs |
| `find_object(type_name)` | first visible object of given type |
| `select_programmable_robot()` | iterates shortcut buttons (evt:1501–1549) until `ButtonOpenStudio` appears |
| `dismiss_satcom(delay=0.3)` | clicks `SatComClose` if visible; returns True if dismissed |
| `open_studio(expected_source=None, robot_filter=None)` | single-pass scan of shortcut buttons; prefers robots with a matching source slot (runnable first, non-runnable accepted); falls back to first robot with ButtonAddProgram; anti-pollution: reuses existing runnable slot instead of adding new one; `robot_filter` lets callers select by content predicate instead of source string |
| `compile(source, slot=-1)` | `set_program` with `compile=True`; returns error dict or `None` |
| `run_program()` | click `ButtonRunProgram` on selected robot's HUD |
| `console(command)` | opens console (`Backquote`), types command, sends `Return` |
| `save_snapshot(name)` | capture and save baseline PNG |
| `assert_snapshot(name, tolerance)` | pixel-diff against baseline; saves `.actual.png` on failure |

### open_studio() robot selection detail

`open_studio` iterates shortcut buttons `evt:1501`–`evt:1549` in a **single pass**:

1. For each robot, check for `ButtonOpenStudio` (dismiss SatCom if it opens automatically).
2. If `robot_filter` is given: call `robot_filter(client)` — accept if True, skip otherwise.  
   Used by prepare hooks that target a robot currently running a specific script.
3. If `expected_source` is given: scan all slots with `GET /program?slot=N`.  
   - Record first **runnable** source-matching slot (`reuse_slot` — best case).  
   - Accept first **non-runnable** source-matching slot too (level data pre-loads the soluce as non-runnable).  
   - Record first **any runnable** slot (`any_runnable` — anti-pollution fallback).  
   - On first source match: stop scanning (this is the target robot).  
   - No match: record robot as `fallback` (stores `any_runnable` for anti-pollution).
4. After scan: select the best target, re-click to confirm, then:
   - If `reuse_slot` found → `select("ListPrograms", index=reuse_slot)`.
   - Else if fallback has `any_runnable` → `select("ListPrograms", index=any_runnable)` (reuse, don't add).
   - Else → `ButtonAddProgram` (only when truly no usable slot exists).
5. Click `ButtonOpenStudio`, wait for `Studio` screen.

**Why this matters:** Without source matching, the Astronaut (Me) can accumulate
program slots from prior levels, appear at a lower `evt:N` than the target robot, and
receive `ButtonAddProgram` incorrectly. The source-match scan ensures the correct robot
is always selected regardless of shortcut button ordering.

---

## 8. Automated Test Suite

Tests live in `tests/agent/` and run with **pytest**.

### 8.1 Test files

```
tests/agent/
├── conftest.py                 # game_server + client fixtures; navigate_to_main_menu()
├── agent_client.py             # HTTP wrapper + AgentClient convenience helpers
├── exercise_helpers.py         # shared: await_win, teardown_to_main_menu, run_exercise_level
├── record.py                   # recording tool (see §10)
├── test_01_health.py           # /health endpoint
├── test_02_state.py            # /state screen detection + widget tree
├── test_03_screenshot.py       # GL and OS screenshot paths; visual regression
├── test_04_navigation.py       # click, type, screen transitions
├── test_05_languages.py        # language list in SetupGame
├── test_06_ingame.py           # studio, cheats (showsoluce, winmission)
├── test_07_win_no_cheats.py    # program the robot to win without cheats
├── test_08_program_api.py      # /launch, rich /objects, /program and /diagnostics
├── test_09_chapter_one.py      # parametrized: all 7 Exercises ch1 levels completed
├── test_10_exercises_all.py    # parametrized: all 31 Exercises ch2–7 levels completed
└── snapshots/                  # baseline PNGs for visual regression
```

**`exercise_helpers.py`** is the shared exercise-test library.  
`test_09` and `test_10` both import from it — no code is duplicated between them.

| Helper | Description |
|--------|-------------|
| `await_win(client, timeout, label)` | Polls `/state` until `screen == "LevelComplete"`; calls `pytest.fail` on timeout |
| `teardown_to_main_menu(client)` | Drives back to `MainMenu` from any in-session screen (SatCom → InGame → Studio → InGame → LevelComplete → LevelSelect → MainMenu) |
| `run_exercise_level(client, category, chap, rank, source, win_timeout, title, prep_hook)` | Full exercise flow: navigate → launch → poll for robot shortcuts (widget-based, no fixed sleep) → set_speed(16) → dismiss_satcom → prep_hook → open_studio → inject source → StudioRun → dismiss_satcom → StudioOK → set_speed(16) → await_win; always calls teardown_to_main_menu in finally |

### 8.2 Running tests

**Prerequisites:** Colobot binary built with `-DTRANSLATIONS=ON`; `pytest requests pillow` installed.

**Against an already-running server (local dev):**
```sh
COLOBOT_AGENT_URL=http://localhost:7777 pytest tests/agent/ -v
```

**Auto-start with Xvfb + llvmpipe (CI or devcontainer):**
```sh
pytest tests/agent/ -v \
  --colobot-bin=./build/colobot \
  --colobot-datadir=./build/data
```

**Update visual regression baselines:**
```sh
COLOBOT_AGENT_URL=http://localhost:7777 pytest tests/agent/ --update-snapshots
```

**Focused iteration (single level):**
```sh
# Run one specific exercise level — fast iteration on a failing test
COLOBOT_AGENT_URL=http://localhost:7777 pytest tests/agent/test_10_exercises_all.py \
  -k "ch04_lvl04" -v -s

# Run a full chapter
COLOBOT_AGENT_URL=http://localhost:7777 pytest tests/agent/test_10_exercises_all.py \
  -k "ex_ch03" -v -s
```

The test IDs follow the pattern `ex_chCC_lvlRR-<title_slug>` so `-k "ch03"` matches
all chapter 3 levels, `-k "ch03_lvl05"` matches exactly one level.

### 8.3 CI integration

The GitHub Actions workflow (`.github/workflows/agent-server-tests.yml`) runs the
full pytest suite on every push to `dev` or `claude/**` and on PRs targeting `dev`.
Screenshots are uploaded as artifacts on every run.

### 8.4 navigate_to_main_menu()

`conftest.navigate_to_main_menu(client)` drives the game back to `MainMenu` from
any reachable screen using the following transitions:

| From | Action |
|------|--------|
| `PlayerSelect` | click `ButtonOK` |
| `SatCom` | click `SatComClose` |
| `Studio` | click `StudioCancel` |
| `LevelComplete` | click `ButtonEndLevel` → `LevelSelect` |
| `InGame` | press `Escape` → `InGameMenu` |
| `InGameMenu` | click `ButtonAbort` |
| `LevelSelect` / `SetupGame` / `SetupDisplay` / `SetupGraphics` / `SetupControls` / `SetupSound` | click `ButtonBack` |

---

## 9. Recording Tool

`tests/agent/record.py` captures a screenshot + state JSON on every screen
transition and widget-set change while you navigate manually.

```sh
# start the game
./build-dev/colobot -datadir build-dev/data/ -agentserver=7777 ...

# record in another terminal
python3 tests/agent/record.py --out tests/agent/recording/my_session/
```

Output per frame: `NNN_ScreenName.png` + `NNN_ScreenName.json`.  
`index.md` lists every frame with widget IDs — useful for discovering new widget names.

Options: `--url`, `--interval` (poll rate), `--every-tick` (save every poll).

---

## 10. Translation Support

### How translated help files work

SatCom documentation lives in `data/help/<lang>/` where `<lang>` is a single
letter (`E` = English, `F` = French, `D` = German, etc.). The game calls
`CLevelParser::InjectLevelPaths()` which replaces `%lng%` with the language char,
then falls back to `E/` if the file is missing.

Translated files are **generated at build time** from `.po` files:

```sh
# Run after cmake configure (requires -DTRANSLATIONS=ON):
make i18n_bots_po i18n_cbot_po i18n_generic_po i18n_object_po i18n_programs_po
```

In `COLOBOT_DEVELOPMENT_MODE` each target automatically copies translated files
from `build/data/help/po/<category>/<lang>/` to `build/data/help/<lang>/` so the
running game finds them (fixed in `data/help/CMakeLists.txt`).

### Verifying translations via agent API

1. Enter a level
2. Select a robot and click `ButtonSatComOpen`; wait for screen `"SatCom"`
3. Click `SatComProg` to open the CBOT programming page (has translations for all languages)
4. Read `SatComContent.value` — first line should differ by language
5. Switch language in `SetupGame → ListLanguage`, re-enter the level, repeat

If `SatComContent.value` is empty or identical after language change, the
translated `.txt` file is missing for that language.

---

## 11. CBot Scripting Gotchas

Known CBot language quirks encountered during test development.  
These affect both test script authoring and official level solutions.

### 11.1 Purged public class type confusion

**Symptom:** After navigating away from a level that defined a public class (e.g.
`exchange`, `order`) and back into the same level, CBot rejects the class name as an
unknown type during compilation of a new program. The error appears even though the
class is defined in the same source file.

**Root cause:** `CBotClass::m_publicClasses` is a global static registry that
persists across level transitions. When a level is torn down, public classes are
purged (removed from live objects) but the `CBotClass` nodes remain in the registry
with `m_IsDef = false`. During the next compilation, the parser finds the stale node
and sees an incomplete (not-yet-defined) class, which it rejects.

The situation is ambiguous: a node with `m_IsDef = false` might be a purged class
from a prior level (reject) or a class in the *same program* that is still being
compiled in a prior pass (accept).

**Fix (implemented):** `CBotInstr::Compile` and `TypeParam` (CBotUtils.cpp) use a
two-condition guard:

```cpp
if (pClass->IsFullyDefined() ||
    pStack->GetProgram()->ClassExists(pClass->GetName()))
```

`IsFullyDefined()` returns true once all three compilation phases complete.
`ClassExists()` returns true for classes belonging to the *current program*, allowing
forward references within a single source file while rejecting stale purged classes.

### 11.2 Integer NaN sentinel (`int m_type = nan`)

**Symptom:** An exchange protocol using `int m_type = nan` as a "no order pending"
sentinel deadlocks at high simulation speed. The put()-side condition
`m_order.m_type == nan` is always false, so `put()` always returns false and the
sending side spins forever.

**Root cause:** `CBotVar::VarIsNAN()` (used by `==` comparisons involving `nan`)
checks for the `InitType::DEF` vs `InitType::NAN` distinction on floating-point
variables only. Integer variables are always stored as integers — assigning `nan` to
an `int` silently truncates to `0` and sets no NaN flag.

**Fix (implemented):**
- Added `CBotVar::InitType::NAN_INT = 3` to the init-type enum.
- `CBotVarInteger::SetValFloat/SetValDouble` now sets `NAN_INT` when the incoming
  float is `std::isnan()` instead of converting to 0.
- `VarIsNAN()` checks for `NAN_INT` on integer types.

**Workaround (test override):** Replace `nan` sentinel with `-1`:

```cbot
int m_type = -1;   // -1 means "empty"; -1 == -1 works correctly
```

### 11.3 ExchangePost race condition at high simulation speed

**Symptom:** A remote-control level passes at normal speed but the slave robot
executes wrong commands (e.g. `move(0)`, `turn(0)`) at high speed (`set_speed(16)`).

**Root cause:** The official controller calls `send("order", ...)` then
`send("param", ...)` in that order. At high speed, CBot can tick the slave robot
between these two `send()` calls: the slave's `testinfo("order")` returns true,
reads `param` (not yet written → 0), executes the command with value 0, then deletes
the info. By the time `send("param", ...)` executes, the slave has already consumed
the order with the wrong parameter.

**Fix (test override):** Send `"param"` before `"order"` so the slave can never
read a half-written message:

```cbot
send("param", param, 100);   // write value first
send("order", order, 100);   // write key second — slave wakes up here
```

### 11.4 `ListStudioPrograms` opens SatCom instead of switching slots

**Symptom:** Clicking `ListStudioPrograms` inside the Studio dialog opens the SatCom
help viewer rather than switching the active program slot.

**Root cause:** The Studio's program list widget is wired to `EVENT_SATCOM_*` — its
click handler opens the SatCom documentation browser, not a slot selector.

**Workaround:** Use `select("ListPrograms", index=N)` on the **HUD** (outside Studio)
*before* opening Studio to set the desired active slot. The slot stays selected when
Studio opens.

---

### 11.5 CBot string character indexing — `s[n]` read and write

**Context:** CBot now supports `s[n]` for both reading and writing individual characters
of a `string` variable (implemented via `CBotStringCharExpr` + `CBotVarStringChar`).

**How it works:**
- `s[n]` on a `string` returns a one-character string.
- `s[n] = "X"` replaces the character at position `n` in `s` in-place.
- `arr[n][m]` on a `string[]` array composes naturally: `arr[n]` yields the n-th element
  (a `string`), then `[m]` applies character access on it — including write-through that
  modifies the array element.

**Type tracking:** The compile-time `var` pointer in `CBotExprVar::Compile` /
`CBotLeftExpr::Compile` advances from `CBotTypArrayPointer` to `CBotTypString` after the
array `[n]` branch; the next `[m]` then hits the string-char branch. No special syntax
is needed: disambiguation is purely type-driven.

**Previously disabled test:** `StringAsArray` (issue #694) is now enabled and passing.

**Boundary behaviour:** Out-of-bounds index emits `CBotErrOutArray` at runtime. Negative
indices are also rejected. The `string` is never extended by assignment through `s[n]`.

**Prior alternative:** `strmid(s, n, 1)` for read; no write equivalent existed.

---

## 12. Integration Points

| Concern | Location | Detail |
|---------|----------|--------|
| CLI flag | `app.cpp` `option options[]` | `OPT_AGENTSERVER` |
| Window creation | `app.cpp` | Always created when `-agentserver` set, even with `-headless`; `SDL_WINDOW_HIDDEN` in headless mode |
| Software GL | `app.cpp` `CreateVideoSurface()` | `SDL_GL_ACCELERATED_VISUAL` skipped in headless mode |
| Main-loop drain | `app.cpp` after `Render()` | `m_agentServer->DrainQueue()` |
| Pre-swap capture | `app.cpp` `Render()` | `CaptureFrameIfPending()` between `m_engine->Render()` and `SDL_GL_SwapWindow()` |
| GL readback | `gl33_device.cpp` `GetFrameBufferPixels()` | `glReadPixels` on FBO 0; rows flipped + RGBA→RGB + libpng encode |
| Widget lookup | `interface.cpp` `SearchControl(EventType)` | Returns `CControl*`; cast to `CEdit`, `CButton`, `CList` |
| Screen detection | `agent_server.cpp` `DetectScreen()` | Infers screen from visible widget IDs |
| Object projection | `engine.cpp` `WorldToInterface()` | Projects world pos through `m_matView` + `m_matProj` to `[0..1]` interface coords |
| Object rich state | `agent_server.cpp` `DoObjects()` | Energy via `GetObjectPowerCell`/`GetObjectEnergyLevel`; shield via `CShieldedObject::GetShield`; task via `CTaskExecutorObject::GetForegroundTask` + `typeid` |
| Level launch | `agent_server.cpp` `DoLaunch()` | `CRobotMain::SetLevel(cat,chap,rank)` + `ChangePhase(PHASE_SIMUL)` |
| Program access | `agent_server.cpp` `DoGetProgram/DoSetProgram` | `CRobotMain::GetSelect()` → `CProgramStorageObject::GetProgram(slot)` → `CScript::GetSource/SetSource/CompileScript` |
| Compile errors | `agent_server.cpp` `ScriptErrorJson()` | `CScript::GetError(string)` + `GetCursor1/2` |
| Widget registry | `agent_server.cpp` `GetRegistry()` | Maps `EventType` integers to stable string IDs; falls back to `"evt:N"` |
| Translation path | `parser.cpp` `InjectLevelPaths()` | Replaces `%lng%` with language char; falls back to `E` |

---

## 13. Spec Evolution Log

| Version | Date | Change |
|---------|------|--------|
| 0.1 | 2026-04-17 | Initial draft from exploratory session |
| 0.2 | 2026-04-18 | Closed OQ-1..4; added integration points table |
| 0.3 | 2026-04-19 | Closed OQ-5 (Linux/Xvfb verified); CI pipeline (Phase 1) |
| 0.4 | 2026-04-19 | GL framebuffer screenshot (Phase 2); dual screenshot modes; automated pytest suite |
| 0.5 | 2026-04-20 | `/objects` endpoint; `SatCom` screen detection; studio open flow (`ButtonAddProgram`); `record.py` tool; `navigate_to_main_menu` SatCom handling |
| 0.6 | 2026-04-20 | Full widget registry (all screens); translation support documented; `SetupDisplay/SetupGame/SetupSound` screens; complete SatCom + Studio widget sets |
| 0.7 | 2026-04-20 | AI validation loop narrative (§1); `/program` GET+POST (§5.9–5.10); `/diagnostics` GET (§5.11); `/launch` planned (§5.12); rich object state planned (§5.8); AgentClient helpers `compile()`, `run_program()`, `get_program()`, `set_program()`, `diagnostics()` |
| 0.8 | 2026-04-20 | `/launch` implemented — `SetLevel`+`ChangePhase(PHASE_SIMUL)` (§5.12); rich object state implemented — `energy`, `shield`, `rotation`, `program_running`, `task` fields in `/objects` (§5.8); AgentClient `launch()` |
| 0.9  | 2026-04-26 | Coordinate systems section (§3.1) — four-layer stack, mapping bug class, test recipe; `POST /mouse_move` (§5.13); `POST /drag` (§5.14); `POST /mouse_button` (§5.15); `GET /window` (§5.16); AgentClient `mouse_move/drag/mouse_down/mouse_up/window()` |
| 0.10 | 2026-04-26 | `test_09_chapter_one.py` — parametrized completion test for all 7 Exercises chapter-1 levels using official solution scripts |
| 0.11 | 2026-05-01 | `test_10_exercises_all.py` — parametrized completion test for all 31 Exercises ch2–7 levels; `exercise_helpers.py` shared library (`await_win`, `teardown_to_main_menu`, `run_exercise_level`); widget-based robot-spawn polling replaces fixed `sleep(1.2)`; per-level source overrides for slow/buggy official scripts (Wasp Hunter 1-2, Labyrinth 1, Remote Control #2 race fix, Remote Control #4 int-nan fix) |
| 0.12 | 2026-05-01 | CBot fixes: purged public-class type confusion (`IsFullyDefined() \|\| ClassExists()` guard, §11.1); integer NaN sentinel support (`InitType::NAN_INT`, §11.2); `open_studio()` rewritten as single-pass scan with `robot_filter` predicate, `any_runnable` anti-pollution fallback, and non-runnable soluce-slot reuse; `dismiss_satcom(delay)` extracted as helper; CBot scripting gotchas documented (§11); focused test run commands added (§8.2) |
| 0.13 | 2026-05-01 | CBot string-char indexing: new `CBotStringCharExpr` instruction + `CBotVarStringChar` proxy implements `s[n]` read and write; `string[][n]` double-subscript composes automatically via compile-time type tracking. `PublicClasses` test enabled (correct `CBotErrUndefClass` on purged-class). `StringAsArray` + `StringArrayCharAccess` tests enabled. 206/206 unit tests passing. |
