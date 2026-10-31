# Manual acceptance checklist

Run these in a real OBS session on the target machine. Items marked **(auto)** are also
covered by the in-OBS self-test (`tests/runtime/run-selftest.sh`); repeat them manually
on each new OBS / OS / GPU combination. Record the OBS version, OS, GPU, canvas size and
FPS at the top of your results.

## Preparation
- [ ] Start the graphics fixture: `python3 tests/fixtures/graphics/fixture-server.py --log loads.jsonl`
      (URL `http://127.0.0.1:8765/overlay.html`), or use the real MatchFrame graphics output URL.
- [ ] Create 1–8 camera inputs in OBS (physical cameras, or `tests/media/cam1..8.mp4` as looping Media Sources –
      **eight distinct feeds**, never eight references to one source).
- [ ] Commentary on a global audio device (Settings → Audio), e.g. a microphone or `tests/media/commentary.wav`.
- [ ] Studio Mode on. Tools → MatchFrame Multiview → Setup: assign cameras, paste the graphics URL, **Apply URL**,
      **Create / Repair**.

## Scenes and graphics
- [ ] (auto) Exactly 10 MF scenes exist; no helper scene. Each camera scene = input, then graphics item above it.
- [ ] (auto) The same `MF — MatchFrame Overlay` source is listed in all eight camera scenes (Sources dock shows one
      source; right-click → Filters on one shows the same object).
- [ ] Browser properties: *Shutdown source when not visible* OFF, *Refresh browser when scene becomes active* OFF,
      size = canvas, page background transparent over the camera.
- [ ] **Browser continuity:** note `page_loads` at `http://127.0.0.1:8765/stats`; perform **≥ 100 cuts across all eight
      camera scenes** (Space / 1–8); `page_loads` unchanged; the overlay clock never resets and the ticker never jumps.
- [ ] Apply URL once: `page_loads` increases by exactly one (explicit reload is allowed).
- [ ] (auto) Graphics ON/OFF hides/shows the overlay on Program and in all eight camera scenes together; the page keeps
      running while hidden (fixture `/stats` → `last_beat` keeps updating, no new load).
- [ ] (auto) With *Show camera graphics on Program only* ticked (default), the multiview's camera tiles and Preview pane
      show the cameras clean while the Program pane, stream and recording show the overlay. Unticked, the tiles show it.
- [ ] With a real Browser Source: set the overlay to a full-screen break graphic → every camera tile still shows its
      camera; cut between cameras during the break → the break graphic stays on Program; `page_loads` unchanged.
- [ ] Take Replay / Highlights → the camera overlay leaves Program; Return Live → it is back on the camera.
- [ ] (auto) Hide one camera's overlay item by hand in OBS → button shows MIXED → press it → all ON.
- [ ] (auto) Crop/scale one camera in OBS, run Repair → framing preserved, graphics still on top.

## Switching
- [ ] (auto) Click each valid tile: only Preview changes. Unassigned tiles are dimmed and refuse selection.
- [ ] (auto) CUT / Space: hard cut, no fade, even if Fade was selected in OBS beforehand (plugin selects Cut and shows it).
- [ ] (auto) Setup → Transitions: set a camera to Fade 800 ms → the button reads TAKE · Fade 800 ms and the take fades;
      Return Live to that camera fades; scenes left on Cut still hard-cut; Repair keeps the setting.
- [ ] With a real Stinger: set Replay to the Stinger → the replay starts under the stinger and the stinger plays fully.
- [ ] (auto) Hold Space / hammer CUT: one take only.
- [ ] (auto) Type `1234` into the Setup URL field and the rename dialog: no switching.
- [ ] Settings → Hotkeys: MatchFrame actions exist and are unbound; bind one, verify it works, restart OBS, binding kept.
- [ ] (auto) Change scenes in OBS's own scene list / T-bar / obs-websocket: monitors, names and tally follow.
- [ ] (auto) Start a 1.5 s Fade in OBS and press CUT during it: refused until the fade finishes.

## Replay / Highlights
- [ ] (auto) Load a replay clip while a camera is live: READY, first frame visible in the tile.
- [ ] (auto) Stage it in Preview, wait **60 s**: time stays 0:00 and **no clip audio** on Program / recording.
- [ ] (auto) CUT: clip plays from its first frame (magenta marker in the test clip) with its audio.
- [ ] (auto) While Replay is live: Load / Arm is refused; Play/Pause and Restart are red “(LIVE)”.
- [ ] (auto) Load a different reel into Highlights while Replay is live; take Highlights; Replay re-arms at 0:00.
- [ ] (auto) Return Live returns to the camera that was live **before** Replay (not the Preview camera).
- [ ] (auto) Native take (double-click the MF — REPLAY scene in OBS): starts once from the beginning.
- [ ] (auto) Let a clip run to the end: ENDED, last frame held, no loop, no automatic scene change.
- [ ] (auto) Load a missing / corrupt file: that slot shows ERROR; cameras unaffected.
- [ ] Half-time and full-time: load two different reels into Highlights in turn and take each live.

## Failures and lifecycle
- [ ] (auto) Delete a camera input: tile shows MISSING INPUT, cannot be selected, no automatic take.
- [ ] (auto) Delete the last-live camera scene while Replay is live: Return Live disabled; Repair recreates the scene
      but Return Live does not jump to it.
- [ ] Unplug a physical camera: tile status stays truthful (no fake “online”); Program continues; operator cuts away. **(hardware)**
- [ ] Stop the graphics server / disconnect the network: switching still works; no automatic refresh.
- [ ] (auto) Open/close the multiview repeatedly, resize, full screen + Esc, move to a second monitor with a
      different DPI: no crash; layout keeps all 12 panes. **(second monitor: manual)**
- [ ] (auto) Close the multiview while recording/streaming: outputs and Program continue.
- [ ] (auto) Restart OBS: assignments, labels, files, last live camera restored; renamed scenes keep their slot.
- [ ] (auto) Switch scene collection and back: the other collection has its own bank; no stale tiles.
- [ ] Quit OBS with the multiview open: clean exit, no "Not all sources were cleared" in the log.

## Audio
- [ ] (auto) Record a session with camera, Replay and Highlights takes; commentary continuous throughout
      (`tests/runtime/analyze-recording.py` automates this for the self-test recording).
- [ ] Camera inputs with embedded audio: confirm the operator's chosen routing (muted in mixer, or following the scene).

## Performance (hardware-dependent)
- [ ] ≥ 30 min with eight camera inputs, two loaded playback files, graphics and recording, with repeated takes.
      Record CPU/GPU, *rendering lag* and *encoding lag* (OBS Stats dock) with the multiview **open vs closed**.
- [ ] Note input-to-Program behaviour per input type (decoding/bandwidth limits are OBS/input properties, not plugin cost).

## Intermission and Video Ads
- [ ] (auto) Create / Repair adds `MF — INTERMISSION` (its page only) and `MF — VIDEO ADS` (its media source only).
- [ ] With a real Browser Source: set the Intermission URL → its tile shows the page; `-` then CUT takes it; Return Live
      goes back to the last camera.
- [ ] (auto) Load two or more ads → READY on ad 1; `=` then CUT → they play in order; after the last: ENDED, no
      auto-switch; Return Live → re-armed at ad 1.
- [ ] Ad audio is heard only while Video Ads is on Program.
