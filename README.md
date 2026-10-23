# MatchFrame Multiview (OBS Studio plugin)

A native OBS Studio plugin for live sports production. It builds a bank of **ten
production scenes**: eight camera scenes, **Replay** and **Highlights**. It opens a native
**twelve-pane multiview**: all ten scene tiles plus the actual OBS **Preview** and
**Program**. Operators stage a tile in Preview, hard-**CUT** it to Program, run prepared
replay / half-time / full-time media, and **Return Live** to the last camera.

OBS still does everything else: capture, composition, encoding, recording and streaming.
The existing MatchFrame graphics output is used as a transparent Browser Source. One
shared instance is referenced in all eight camera scenes, so the clock, score and
animations keep running across cuts. The plugin adds no streaming engine and no MatchFrame
API, endpoints or credentials.

> **Status – read first.** It has been built and runtime-tested only on **Linux x86_64
> with OBS 30.2.3**, under a virtual X server with software OpenGL. Windows and macOS
> builds are configured by the official template but **have not been built or tested**.
> The Ubuntu OBS package used for testing has **no Browser Source**, so browser-specific
> checks are **unrun**: page-load continuity over 100 cuts, and the shutdown/refresh
> behaviour. The shared-graphics code paths were exercised with a stand-in source
> instead. Physical cameras, device loss and real-GPU performance are also **pending**.
> See [docs/TEST-RESULTS.md](docs/TEST-RESULTS.md) and
> [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

---

## 1. What you get

| Scene (default name) | Contents, bottom → top | Default tile label |
|---|---|---|
| `MF — CAM 01` … `MF — CAM 08` | your existing OBS camera input → `MF — MatchFrame Overlay` (shared Browser Source reference) | Main Wide, Tight / Follow, Left Goal, Right Goal, Sideline, Reverse Angle, Bench / Crowd, Handheld / Spare |
| `MF — REPLAY` | `MF — Replay Media` (Media Source) → optional `MF — Replay Graphics` | Replay |
| `MF — HIGHLIGHTS` | `MF — Highlights Media` (Media Source) → optional `MF — Highlights Graphics` | Half-Time / Full-Time Highlights |

* There are exactly ten scenes and no hidden "graphics helper" scene. Unrelated scenes in
  your collection are left alone.
* The scenes are ordinary OBS scenes. You can rename them and edit them; ownership is
  tracked by UUID and a private marker, never by name.
* Camera inputs are **references** to inputs you already created in OBS. The plugin never
  creates a second capture or decoder for the same device.

**Tools → MatchFrame Multiview** opens one top-level window. It holds a single OBS render
surface, with Qt controls kept outside it:

```
┌ CUT · RETURN LIVE → CAM n · Camera Graphics: ON/OFF/MIXED · Transition · STREAMING · RECORDING · Setup… · Full Screen ┐
│        [ PREVIEW — scene name ]  (green)          [ PROGRAM — scene name ]  (red)        │
│        [ 1 ] [ 2 ] [ 3 ] [ 4 ]                                                          │
│        [ 5 ] [ 6 ] [ 7 ] [ 8 ]                                                          │
│              [ REPLAY ] [ HIGHLIGHTS ]                                                   │
├ REPLAY  file · READY/PLAYING/PAUSED/ENDED/ERROR · 0:03 / 0:08 · Load / Arm… · Arm · Play · Restart ┤
└ HIGHLIGHTS …                                                                                    ┘
```

Every tile shows its composed OBS scene, graphics included. Tiles carry text **PREVIEW**
and **LIVE** badges plus green and red borders, so tally never relies on colour alone. The
red monitor is the Program **bus**. **STREAMING** and **RECORDING** are separate indicators
read from OBS's real output state.

## 2. Install / uninstall

### Linux (built and tested: x86_64, OBS 30.2.x)

Per-user install from `dist/matchframe-multiview-0.1.0-linux-x86_64.tar.gz`:

```bash
mkdir -p ~/.config/obs-studio/plugins
tar -xzf matchframe-multiview-0.1.0-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins
# result: ~/.config/obs-studio/plugins/matchframe-multiview/bin/64bit/matchframe-multiview.so
#         ~/.config/obs-studio/plugins/matchframe-multiview/data/locale/en-US.ini
```

Or install system-wide with the Debian package (`dist/matchframe-multiview-0.1.0-x86_64-linux-gnu.deb`):
`sudo apt install ./matchframe-multiview-0.1.0-x86_64-linux-gnu.deb`.

Restart OBS. **Tools → MatchFrame Multiview** should appear, and the OBS log should show
`[matchframe-multiview] plugin loaded successfully`.

Uninstall: delete `~/.config/obs-studio/plugins/matchframe-multiview`, or run
`sudo apt remove matchframe-multiview`. Then restart OBS. Scenes the plugin created stay
in your scene collection as ordinary scenes; delete them in OBS if you no longer want them.
Plugin preferences live in `~/.config/obs-studio/plugin_config/matchframe-multiview/`
(window geometry and hotkey bindings).

The binary is built against OBS **30.2.3** headers. OBS plugins are not ABI-guaranteed
across major versions, so build from source for any other OBS version (see §8).

### Windows / macOS

**Not built or tested.** The template's build scripts and CMake presets are intact
(`windows-x64`, `macos`). See §8 and docs/COMPATIBILITY.md.

## 3. First-time setup

1. **Create your inputs in OBS as usual.** Add each camera as a Source (Video Capture
   Device, NDI/SRT/RTSP via Media Source or a third-party plugin, …). A good place is a
   working scene of your own. The plugin only references inputs; it never configures
   devices.
2. Open **Tools → MatchFrame Multiview → Setup…**
   * **Camera scenes:** pick an input for each of slots 1–8. One to eight cameras all work.
     Unused slots stay visible as `UNASSIGNED` and can't be selected. If you assign the
     same input to two slots, you get a warning: it's the same feed, not an extra camera.
     **Fit** / **Fill** reset that camera's framing. Otherwise your crop, position and scale
     edits in OBS are never touched.
   * **Camera graphics:** paste the existing MatchFrame **graphics output URL**. That's
     the overlay page, not the public homepage. Press **Apply URL**. The field is masked
     because URLs can carry tokens. The plugin never logs a full URL: query strings and
     deep paths are redacted.
   * Press **Create / Repair MatchFrame Scenes**.
3. Make sure **Studio Mode** is on. The multiview offers **Enable Studio Mode** if it's off;
   plugin takes are disabled without it.

**Create / Repair** is idempotent. It reuses plugin-owned objects and recreates only what's
missing. It restores the graphics item above the camera, clears per-scene transition
overrides on the ten owned scenes, and keeps your manual framing, filters and input
properties. It never switches Program and never starts an output. If a default name is
taken by an unrelated object, it uses `… (2)` and reports it. It is **disabled while
streaming or recording**, and so are input assignments. Switching, loading media into an
off-air playback slot, transport and graphics visibility all still work then.

Browser Source settings the plugin applies: transparent background CSS, width/height equal
to the **current OBS base canvas** (the canvas itself is never changed), **Shutdown source
when not visible = OFF**, **Refresh browser when scene becomes active = OFF**. In
obs-browser, changing URL, CSS or those two flags reloads the page. So the plugin changes
them only on **Apply URL**, or when Repair has to correct a wrong flag, and Repair tells you
when that reloads the page. Camera cuts never touch the browser.

## 4. Operating

| Action | Result |
|---|---|
| Click a camera / Replay / Highlights tile | Puts that scene in OBS **Preview**. Program is unchanged. |
| **CUT** button or **Space** | Hard cut of the actual OBS Preview to Program (selects OBS's *Cut* transition) |
| Keys **1–8** | Preview camera 1–8 |
| Key **9** / **0** | Preview Replay / Highlights (must be READY) |
| **Return Live** | **Immediate on-air cut** to the last camera that was actually on Program. Preview is left as it is. |
| **Esc** | Leave full screen |
| Double-click a tile | Edit its label |
| Native OBS changes (scene list, Studio Mode T-bar, obs-websocket, hotkeys) | Monitors, names and tally follow OBS's real state |

* Shortcuts work only while the multiview window has focus. Held-key auto-repeat is
  ignored. Typing in any field, picker or dialog never switches.
* **OBS hotkeys** (Settings → Hotkeys): *MatchFrame: Preview Camera 1–8*, *Preview
  Replay*, *Preview Highlights*, *CUT*, *Return Live*. They're **unbound by default**,
  to avoid collisions and accidental takes.
* CUT is serialized. Repeated presses while a take is in progress are dropped, not queued.
  CUT is refused while another OBS transition is still running.
* CUT is refused, with a message, when:
  - the Preview camera is unassigned or its input was deleted;
  - a playback scene is not READY;
  - the Preview scene has a per-scene transition override that isn't Cut. The plugin never
    silently fades; remove the override or run Repair.
* If Preview and Program are the same scene, CUT does nothing and never restarts media.
* After a cut, the plugin shows whatever OBS really did. That includes OBS's "swap Preview
  and Program" preference.
* **Camera Graphics ON/OFF** sets the plugin's overlay item visibility in all eight camera
  scenes at once. It also covers the scene instance currently on Program, since Studio
  Mode's default *Duplicate Scene* puts a private copy there. The page stays loaded. If
  you edit visibility by hand in OBS and the scenes disagree, the button shows **MIXED**
  until you apply a global state. This control doesn't affect the Replay/Highlights
  overlays.

### Replay and Highlights

1. While a camera is live, press **Load / Arm…** on the Replay (or Highlights) panel and
   choose the prepared file. The clip opens, **pauses on its first frame** and shows
   **READY**. Loading and arming are off-air: the media source isn't in the output, so
   none of its audio reaches Program. Plugin-created media sources also have monitoring OFF.
2. Click the tile (or press 9/0) to stage it in Preview. Selecting it doesn't advance it.
   Tests confirmed it stays at 0:00 through a 60-second Preview wait.
3. Press **CUT**. When OBS reports the playback scene on Program, the clip starts **from
   the beginning**. A native OBS take into the scene starts it too, exactly once.
   Resizing, Preview changes and thumbnail refreshes never restart it.
4. Status shows **PLAYING / PAUSED / ENDED / ERROR** with elapsed time and duration. At
   the end, the clip holds its last frame, shows **ENDED**, and **does not loop or
   auto-switch**.
5. **Return Live** cuts to the last real camera. The camera you were on before
   Replay → Highlights is remembered across both, and Preview is never mistaken for it.
   If that camera's scene or input was deleted, Return Live is disabled; it never picks a
   substitute.

When a playback scene leaves Program, its media pauses and re-arms from the beginning for
the next take. Load / Arm and seeking are **blocked while that source is on Program**.
**Play/Pause** and **Restart** stay available, and turn red with **(LIVE)** labels when they
affect the output. Replay and Highlights use separate media sources and playheads. You can
load the next half-time reel into Highlights while Replay is live. Off-air Play/Pause
disarms a slot; press **Arm** before taking it.

Camera graphics (live clock/score) are **not** placed on playback scenes, because a live
clock over a replay would mislead. Each playback scene can instead have its own optional
overlay URL (e.g. "REPLAY" branding) in Setup, independent of the camera toggle.

## 5. Audio routing (configure in OBS)

* Put **commentary and the master programme mix on OBS global audio devices** (Settings →
  Audio → Mic/Auxiliary / Desktop audio). Those stay active whichever scene is on Program,
  so camera cuts never change them. The self-test drives commentary through such a global
  channel and verified it was continuous across every camera and playback take.
* The plugin never mutes, unmutes or edits the audio properties of your camera inputs.
  If a camera input carries embedded audio, that audio is part of its scene and follows
  that scene on and off Program; this is standard OBS behaviour. To keep camera audio off
  the broadcast, mute it in OBS's Audio Mixer (or Advanced Audio Properties).
  Audio-follow-video is out of scope.
* Replay/Highlights clip audio reaches the Program tracks only while the playback source is
  actually on Program. Set clip volume and mute in OBS's mixer. There is no automatic
  commentary ducking.

## 6. Failure behaviour

* **Deleted input:** the camera tile shows `MISSING INPUT` and can't be selected. A scene
  on Program keeps running; there's no automatic take.
* **Signal loss on an existing input:** shown as unknown. Black pixels are never treated as
  proof of disconnection.
* **MatchFrame page or network unavailable:** switching keeps working. The plugin reports
  configuration status only, never a fake "connected" heartbeat, and never auto-refreshes
  during a show.
* **No Browser Source in this OBS build:** a clear banner appears. Camera switching and
  playback still work, without graphics.
* **Missing or invalid media file:** only that playback slot shows ERROR. Cameras and
  Return Live are unaffected.
* **Closing the multiview** releases only its own render references. OBS, Program,
  recording and streaming keep running. The plugin never starts or stops outputs.
* **Scene collection change:** switching is suspended and all render and source references
  are released. The new collection's own bank is then loaded; a brand-new collection
  starts empty. The plugin never carries pointers or settings across collections.

## 7. Persistence

* Per scene collection, saved inside the collection file through OBS's save callback:
  assignments, scene/source/item UUIDs, labels, file paths, graphics URL(s), the graphics
  state and the last live camera. The schema is versioned; v1 (name-only) configs migrate
  to v2 (UUIDs).
* Per user, in plugin config: window geometry and hotkey bindings.
* No graphics URL, token or secret is ever committed to this repository.

## 8. Building from source

Requires CMake ≥ 3.28, a C++17 compiler, Qt 6, and libobs + obs-frontend-api
headers/configs for your OBS version. This project was scaffolded from the official
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) at revision
`3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc`.

```bash
# Linux (tested: Ubuntu 24.04, libobs-dev 30.2.3 from noble-backports, Qt 6.4.2, GCC 13.3)
sudo apt install -t noble-backports obs-studio libobs-dev
sudo apt install cmake ninja-build qt6-base-dev qt6-base-private-dev
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
cpack --config build_x86_64/CPackConfig.cmake      # .deb into release/
```

Options:
* `-DMF_BUILD_TESTS=ON`: builds the unit tests (`build_x86_64/matchframe-unit-tests`).
* `-DMF_ENABLE_SELFTEST=ON`: builds the in-OBS automated self-test (test builds only;
  inert unless `MATCHFRAME_SELFTEST_OUT` is set).

Windows / macOS follow the template (`cmake --preset windows-x64` / `macos`, which
downloads the OBS 31.1.1 sources and obs-deps pinned in `buildspec.json`). They're
untested; `src/render-surface.cpp` contains the native-window handling for each platform.

## 9. Testing

* **Unit tests**: layout and hit testing at several sizes and DPIs, switching decisions,
  repeated-request suppression, last-camera tracking, the playback arm/Program-entry
  state machine, and config round-trip and migration. `cmake --preset ubuntu-x86_64
  -DMF_BUILD_TESTS=ON && cmake --build build_x86_64 && build_x86_64/matchframe-unit-tests`
* **In-OBS automated self-test**: drives the real plugin inside a real OBS against eight
  moving test feeds and the two test clips. It records Program and analyses the recording
  for audio and first-frame markers. Phase 2 restarts OBS and checks persistence and
  collection switching. Run with `tests/runtime/run-selftest.sh build_x86_64 /tmp/mf-run`.
* **Graphics fixture**: `tests/fixtures/graphics/` holds a transparent scoreboard,
  animation, clock and per-load-ID page, plus a server that counts real page loads. Use it
  for the browser-continuity checks on an OBS that has Browser Source.
* **Test media**: `tests/media/` (regenerate with `make-test-media.sh`).
* **Manual checklist**: [docs/ACCEPTANCE-CHECKLIST.md](docs/ACCEPTANCE-CHECKLIST.md).
* **Results**: [docs/TEST-RESULTS.md](docs/TEST-RESULTS.md).

## 10. Code map

| Component | Files |
|---|---|
| Plugin entry (module lifecycle, Tools menu, events, save callback) | `src/plugin-main.cpp` |
| Controller (event hub, hotkeys, persistence glue) | `src/controller.*` |
| Multiview window (Qt controls, shortcuts, full screen) | `src/multiview-window.*` |
| Render surface (one `obs_display_t`, twelve viewports, snapshots) | `src/render-surface.*` |
| Layout calculator (pure) | `src/core/layout.*` |
| Scene bank (ten scenes, ownership, identity, repair) | `src/scene-bank.*`, `src/obs-util.*` |
| Graphics controller | `src/graphics-controller.*` |
| Switch controller + pure switch logic | `src/switch-controller.*`, `src/core/switch-logic.*` |
| Playback controller + pure playback state machine | `src/playback-controller.*`, `src/core/playback-logic.*` |
| Settings store (per-collection config, migrations) | `src/core/config.*` |
| Setup dialog | `src/setup-dialog.*` |

Out of scope (deferred): rolling replay capture, clip editing, slow motion, playlists,
automatic highlights, ISO recording, PTZ, camera discovery, new NDI/SRT/RTSP
implementations, audio-follow-video, ducking, remote switching, Stream Deck, multiview
streaming, dissolves and stingers (only hard cuts are implemented, since a shared overlay
across a dissolve needs separate design), and graphic design/editing.

License: GPL-2.0-or-later (as the OBS plugin template). OBS's own display/projector code
was used as a reference for the native-window handling pattern; no OBS UI classes are
linked.
