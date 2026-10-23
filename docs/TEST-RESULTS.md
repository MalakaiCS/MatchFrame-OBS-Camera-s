# Test results

Environment for every run below: Ubuntu 24.04.5 x86_64 (4 vCPU cloud VM), **OBS 30.2.3**
(Ubuntu noble-backports package, no Browser Source), Qt 6.4.2, GCC 13.3. The display was
Xvfb 1920×1080 with **Mesa llvmpipe software OpenGL**. OBS canvas was 1920×1080 base,
1280×720 output, 30 fps. Raw machine-readable results are in [`results/`](results/).

Legend: **PASS** = verified · **UNRUN** = not executed here (with the reason) ·
**PENDING (hardware)** = needs physical devices / a GPU machine.

## 1. Automated unit tests (no OBS needed)

`build_x86_64/matchframe-unit-tests`: **27 tests, 2113 checks, 0 failures.**

They cover:
* layout and hit testing at 1080p, 4K, the minimum size, portrait and 4:3/21:9 canvases,
  and DPR 1/1.25/1.5/2;
* Preview-select / CUT / Return Live decision rules;
* take serialization (20 rapid requests → 1 take, timeout release);
* last-camera tracking across Replay ↔ Highlights;
* the playback arm → READY → Program-entry → re-arm state machine, including the 60 s idle,
  native take, on-air protection, errors/timeouts, the restart grace period and
  independent slots;
* config round-trip through JSON, defaults, v1 → v2 migration, and URL redaction.

## 2. Automated runtime tests in real OBS (`tests/runtime/run-selftest.sh`)

The self-test drives the real plugin inside OBS 30.2.3 with eight **distinct** moving test
feeds (`tests/media/cam1..8.mp4` as looping Media Sources) and the two test clips.
Commentary is a looping 440 Hz tone on a global audio channel. OBS records Program and
`analyze-recording.py` checks the recording. Phase 2 restarts OBS.

**Final run (final binary):** phase 1 **42/42 PASS**, phase 2 **8/8 PASS**, recording
analysis **9/9 PASS** (158 s recording).

**Long run (30-min soak, previous build):** phase 1 41/42 and phase 2 8/8. 449 takes ran
across cameras, Replay, Highlights and Return Live, with 0 rejected. Commentary was
continuous over the whole 2143 s recording. Two checks failed in that run and both were
traced and fixed before the final run:
* **C19:** the check measured UI-thread latency instead of playback. Investigating it also
  exposed a real cosmetic bug: after a restart, the status briefly showed PAUSED on air.
  Both are fixed.
* **A5:** Highlights was held on air for only 0.96 s, too short for the analysis window.
  The test now holds it for 3 s. The recording showed the reel playing (V3 PASS).

During the soak, a compile job ran concurrently for a few minutes.

| ID | Check | Result |
|---|---|---|
| C01 | Exactly ten plugin-owned scenes (8 cameras + Replay + Highlights) | PASS |
| C01b | Name collision with an unrelated object → `MF — CAM 05 (2)` + warning | PASS |
| C02 | One graphics source shared by all eight camera scenes; no helper scene | PASS* |
| C03 | Each camera scene: own input at the bottom, shared graphics item directly above | PASS* |
| C04 | Replay/Highlights: own media source, no camera graphics; restart-on-activate / loop / close-when-inactive OFF; monitoring OFF | PASS |
| C05 | Graphics source: shutdown-when-hidden OFF, refresh-on-active OFF, sized to canvas | PASS* |
| C06 | Repair ×3: no duplicates (25→25 sources, 0 created), manual crop/position/scale preserved | PASS |
| C07 | Graphics restored above the camera after edits; camera framing kept | PASS* |
| C08 | Clicking each valid tile changes Preview only | PASS |
| C09 / C09b | Unassigned slot cannot be selected (7 cameras); 8th assignment completes the bank | PASS |
| C10 | CUT uses the Cut transition even when Fade was selected; hard cut | PASS |
| C11 / C11b | Non-Cut per-scene override blocks CUT with a message; Repair clears overrides | PASS |
| C12 | 20 rapid CUTs → exactly one take | PASS |
| C13 | Number keys select Preview; auto-repeat ignored | PASS |
| C14 | Typing in the URL field never selects/takes (key reached the field, Preview unchanged) | PASS |
| C15 | One toggle hides/shows all eight owned items **and** the Program duplicate; source untouched | PASS* |
| C16 | Manual per-scene edit → MIXED → global apply | PASS* |
| C17 | Load arms paused at the first frame (READY), source not active | PASS |
| C18 | Replay in tile + Preview for **60 s**: max media time 33 ms, never active | PASS |
| C19 | CUT → Program → starts at 0 (media time 0 ms, observed 62 ms after the take), one start | PASS |
| C20 | Preview change, resize and full screen during playback: no restart | PASS |
| C21 | File replace / arm blocked while on Program | PASS |
| C22 | Off-air Highlights reel replaced while Replay is live | PASS |
| C23 | Replay leaving Program pauses and re-arms at 0; Highlights plays once | PASS |
| C24 | Last live camera preserved across Replay ↔ Highlights, not the Preview identity | PASS |
| C25 | Return Live → last live camera; slot re-armed | PASS |
| C26 | **Native** OBS take into Replay starts it once | PASS |
| C27 | End of clip: ENDED, no loop, no automatic scene change | PASS |
| C28 | Native OBS scene change updates Program / tally / last camera | PASS |
| C29 | Plugin take refused while an external 1.5 s Fade runs | PASS |
| C30 | After an external fade, plugin CUT re-selects Cut | PASS |
| C31 | Missing media: only that slot errors; camera cut still works | PASS |
| C32 | Deleted camera input: MISSING, not selectable, Return Live disabled, no automatic take | PASS |
| C33 / C33b | Deleted last-live camera scene: Return Live disabled; Repair recreates the scene with a new identity and Return Live still refuses it | PASS |
| C34 | Repair never switches Program | PASS |
| C35 | Renamed bank scene keeps its slot and identity | PASS |
| C36 | 10× open/close + 4 resizes: display renders; 0 scenes left showing after close | PASS |
| C37 | Closing the multiview leaves Program and recording running | PASS |
| C39 | Repair and assignment refused while recording; structure unchanged | PASS |
| P01–P05 | After an OBS restart: 10 scenes, UUID assignments, renamed scene, labels / URL / files, last live camera, playback re-armed | PASS |
| P06–P08 | Switching to a new collection gives an empty bank with no stale references; switching back restores the bank; playback re-armed | PASS |
| A1 | Commentary tone continuous across every camera and playback take | PASS (100 % of windows; 2143 s in the soak) |
| A2 / V1 | No clip audio and no clip picture on Program during the 60 s Preview wait | PASS |
| A3 / A4 | Replay audio only after its take, none during load / arm | PASS |
| V2 / V3 | First-frame markers (magenta / yellow) present right after each take | PASS |
| A5 / A6 | Highlights audio on Program; replay audio stops when it leaves; highlights stop after Return Live | PASS |

\* = run with the **stand-in graphics source** (`color_source_v3`, via the self-test-only
`MATCHFRAME_TEST_GRAPHICS_SOURCE` override), because this OBS has no Browser Source. The
code paths are the same ones the real Browser Source uses. Browser-only behaviour is listed
as UNRUN below.

Also verified: no "Not all sources were cleared" in any OBS log, on collection switches or
at exit. An earlier iteration of the test found exactly that problem, a render-thread
reference race, and it was fixed.

## 3. Packaging (manual, scripted here)

| Check | Result |
|---|---|
| Release build (self-test compiled out) under the template's `-Werror` | PASS |
| `dist/…-linux-x86_64.tar.gz` extracted into a clean `~/.config/obs-studio/plugins` → OBS logs "plugin loaded successfully" | PASS |
| `dist/….deb` installs into `/usr/lib/x86_64-linux-gnu/obs-plugins`, OBS loads it, `apt remove` removes it | PASS |

## 4. Performance (this VM only, not representative)

Software OpenGL on 4 vCPUs. Eight 640×360 test feeds, two loaded clips, stand-in graphics,
and x264 recording at 1280×720/30.

| Window | Render frames | Render lagged | Encode skipped | Avg frame render | CPU (process) |
|---|---|---|---|---|---|
| Multiview **closed**, 30 s (final) | 900 | 508 | 0 | 75 ms | 82 % |
| Multiview **open**, 30 s (final) | 900 | 632 | 0 | 106 ms | 75 % |
| Multiview closed, 120 s (soak run) | 3602 | 2128 | 0 | 84 ms | 84 % |
| Multiview open, 120 s (soak run) | 3601 | 2619 | 0 | 119 ms | 76 % |
| 30-min soak with 449 takes | – | 38 990 | – | – | – |

On this machine OBS already lags rendering with the multiview **closed**. The extra
software rendering of twelve panes adds roughly 15–25 % more lagged frames. No encoded
frames were skipped. These figures describe CPU-rendered OpenGL and cannot predict GPU
behaviour, where the multiview's cost is one extra render pass per frame. **A GPU-machine
measurement with real inputs is PENDING (hardware).**

## 5. Not run here / pending

| Item | Status | Why / how to close |
|---|---|---|
| Browser Source page continuity over ≥ 100 cuts (fixture load-ID count) | **UNRUN** | OBS build has no obs-browser. Use an official / PPA / Flatpak OBS + `tests/fixtures/graphics/` (checklist). |
| Browser shutdown / refresh flags in a live CEF instance; Apply URL reload count | **UNRUN** | Same reason. |
| Graphics fixture page itself | PASS (headless Chromium: transparent, clock running, load ID reported to the server) | – |
| Windows / macOS build and runtime | **UNRUN** | Only Linux built. |
| Native Wayland display | **UNRUN** | Tested on X11 (Xvfb) only. |
| High-DPI / second monitor / display change on real screens | **PENDING (hardware)** | DPR math covered by unit tests only. |
| Eight physical cameras, device loss, input-to-Program latency | **PENDING (hardware)** | Test feeds were Media Sources. |
| ≥ 30 min performance record on documented production hardware | **PENDING (hardware)** | 30-min soak ran on the VM (above), but not on a GPU machine. |
| OBS hotkey binding persistence (Settings → Hotkeys) | **UNRUN** | Registration exercised at load; binding UI not automated. |
| Streaming output | **UNRUN** | Only recording was used; streaming state shown from the same frontend API. |
