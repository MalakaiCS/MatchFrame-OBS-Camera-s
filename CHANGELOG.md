# Changelog

## v1.1.0 — 2026-10-23

### Added
- **MatchFrame Replay Cue integration.** New *Replay / Highlights source* option in Setup.
  With it on, the multiview drives the Replay Cue plugin through its `ReplayCue`
  obs-websocket vendor requests, called in-process:
  - the tiles show Replay Cue's Replay and Highlights scenes and live status;
  - **9** cues the selected replay into Preview, and **CUT** plays it with Replay Cue's own
    transition;
  - **0** stages the highlights reel, and **CUT** plays it;
  - **Return Live** uses Replay Cue's return, falling back to the last live camera;
  - new panel controls: Instant Replay, Mark IN/OUT, previous/cue/next, Play/Pause,
    25–100 % slow motion, angle ◀ ▶, Play Highlights.

  The built-in prepared-file playback is unchanged and remains the default.

### Changed
- **Multiview fills the screen.** The layout now chooses between two arrangements for the
  window's actual size:
  - **Wide:** monitors on top; cameras 1–4 + Replay; cameras 5–8 + Highlights.
  - **Stacked:** the original four rows, with a compact playback row at the bottom.

  On 16:9 and wider screens the wide arrangement drops a row, so Preview/Program and every
  tile get larger: on a 1080p monitor the monitors grow by about a third. Tall or narrow
  windows keep the stacked arrangement. The layout is recalculated on every resize,
  full-screen toggle and screen/DPI change.

## v1.0.0 — 2026-10-23

First release of MatchFrame Multiview, a native OBS Studio plugin for live sports switching.

### Features
- **Ten-scene bank:** eight camera scenes (`MF — CAM 01…08`), `MF — REPLAY` and `MF — HIGHLIGHTS`.
  - Owned by UUID plus a private marker, never by name, so renaming a scene doesn't break it.
  - Idempotent **Create / Repair** keeps manual framing, filters and unrelated sources, and
    reports name collisions.
  - Structural setup is disabled while streaming or recording.
- **Shared camera graphics:** one MatchFrame Browser Source sits above the camera in all
  eight camera scenes.
  - Shutdown-when-hidden and refresh-on-active are off, so cuts never reload the page.
  - One **Camera Graphics ON/OFF/MIXED** control, which also covers the scene instance on
    Program in Studio Mode.
  - Explicit **Apply URL**; URLs are redacted in logs.
  - Optional, independent Replay and Highlights overlays.
- **Native twelve-pane multiview** (Tools → MatchFrame Multiview):
  - eight cameras, Replay, Highlights, and the actual OBS Preview and Program in one native
    display;
  - text PREVIEW/LIVE tally plus borders;
  - STREAMING and RECORDING read from OBS's real output state;
  - full screen, DPI-aware layout, and editable tile labels.
- **Switching:**
  - Studio Mode Preview select, and a serialized hard **CUT** that selects OBS's Cut
    transition;
  - per-scene override protection, and refusal while an external transition runs;
  - **Return Live** to the last camera actually on Program;
  - keys 1–8 / 9 / 0 / Space / Esc, with typing protection;
  - OBS hotkeys, unbound by default.
- **Replay and Highlights playback:**
  - a prepared file arms paused on its first frame (READY);
  - it starts from the beginning when it actually reaches Program, by plugin CUT or a native
    OBS take, and re-arms when it leaves;
  - loading is blocked while the clip is on air, and Play/Pause/Restart are labelled (LIVE);
  - no off-air audio, no looping, ENDED state shown.
- **Persistence:** saved per scene collection, with a versioned schema (v1 name-only configs
  migrate to v2 UUIDs). Window geometry and hotkey bindings are stored per user.

### Tested
Tested on Linux x86_64 with OBS 30.2.3 under Xvfb with software OpenGL:
- 27 unit tests;
- in-OBS self-test: 42/42 checks, then 8/8 after restart and collection switching;
- Program-recording analysis: 9/9 (continuous commentary, no off-air clip audio, first-frame
  starts);
- a 30-minute soak with 449 takes.

Details: [docs/TEST-RESULTS.md](docs/TEST-RESULTS.md).

### Known limitations
- **Only the Linux x86_64 / OBS 30.2.x build has been tested.** CI builds Windows and macOS
  packages from the template, but they are untested.
- **Browser Source continuity is untested.** The test OBS had no obs-browser. The
  shared-graphics paths were tested with a stand-in source; use `tests/fixtures/graphics/`
  to verify that page loads stay unchanged over 100 cuts.
- Physical cameras, device loss, high-DPI and multi-monitor setups, and GPU performance are
  pending hardware tests.
- Only hard cuts are supported; there are no dissolves or stingers.
