# Compatibility notes

## Pinned and recorded versions

| Item | Value |
|---|---|
| Plugin template | `obsproject/obs-plugintemplate` @ `3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc` (2025-12-09) |
| Template `buildspec.json` (Windows/macOS deps, unchanged) | OBS sources 31.1.1, obs-deps / Qt6 2025-07-11 |
| OBS SDK used for the Linux build | `libobs-dev` **30.2.3+dfsg-3~bpo24.04.1** (Ubuntu noble-backports), `LIBOBS_API_VER` 30.2.3 |
| OBS runtime tested | `obs-studio` **30.2.3** (same package; Debian build **without** obs-browser/CEF, with obs-websocket) |
| Qt | 6.4.2 (Ubuntu `qt6-base-dev`), the Qt the tested OBS runtime links against |
| Compiler / build | GCC 13.3.0, CMake 3.28.3, Ninja, C++17, `-Werror` (template default) |
| Target architecture | x86_64 (Linux) |
| OS | Ubuntu 24.04.5 LTS, kernel 6.18 (cloud VM) |
| Display / GPU | Xvfb 1920×1080 virtual X server, **Mesa llvmpipe 25.2.8 (software OpenGL 4.5)**, OBS EGL/X11 platform |

## Platform support

| Platform | Implemented | Built | Tested |
|---|---|---|---|
| Linux x86_64, X11 (EGL) | yes | yes | **yes** (software GL under Xvfb) |
| Linux, native Wayland session | yes (`Qt6::GuiPrivate` QPA surface lookup, compiled in) | yes | **no** |
| Windows x64 | yes (HWND path in `render-surface.cpp`; template presets) | **no** | **no** |
| macOS (arm64/x86_64) | yes (NSView path; template presets) | **no** | **no** |
| Linux aarch64 | not configured | no | no |

## OBS API usage and version notes

* Uses only public installed headers: `obs.h`, `obs.hpp`, `obs-frontend-api.h`,
  `obs-module.h`, `obs-nix-platform.h`, `graphics/*`, `util/*`. No private OBS UI classes.
* UUID APIs (`obs_source_get_uuid`, `obs_get_source_by_uuid`) require OBS ≥ 30.0.
* `obs_sceneitem_set_bounds_crop` (Fill framing) is used when `LIBOBS_API_VER ≥ 30.1`.
* Studio Mode preferences are read from the global config (OBS 30.x) or the user config
  (`obs_frontend_get_user_config`, OBS ≥ 31), selected at compile time.
* Behaviour verified by reading the OBS **30.2.3** and **32.0.4** frontend sources:
  - `obs_frontend_set_current_preview_scene` / `obs_frontend_preview_program_trigger_transition`
    / `obs_frontend_set_current_transition` / `obs_frontend_set_current_scene` are
    synchronous when called from the UI thread (Qt auto connection) in both versions.
  - Per-scene transition overrides are stored in the scene's private settings
    (`"transition"`, `"transition_duration"`) and applied by the frontend on take: the plugin
    refuses CUT when an override names a non-Cut transition and Repair clears them on the ten
    owned scenes.
  - Studio Mode *Duplicate Scene* (default ON) puts a private duplicate of the scene on
    Program (`OBS_SCENE_DUP_PRIVATE_REFS`: same sources). The graphics toggle therefore also
    updates the items in the Program instance. *Duplicate Sources* (`EditPropertiesMode`,
    default OFF) would copy sources on take; the multiview shows a warning when it is ON.
  - `SCENE_CHANGED` is emitted after the transition finishes; the plugin's take state
    returns to idle on `TRANSITION_STOPPED`/`SCENE_CHANGED` (2 s safety timeout).
  - A **newly created** scene collection is not loaded through the save callbacks; the
    plugin starts an empty configuration in that case (found by the runtime test).
* Media Source (`ffmpeg_source`) behaviour verified in OBS 30.2.3 source and at runtime:
  with `restart_on_activate=false` the source starts decoding as soon as a file is set, so
  the plugin pauses it immediately and seeks to 0; seek/pause require an active decoder, so
  re-arming an ENDED clip reopens the file first; `restart` is ignored for a source that
  is not showing (the plugin reopens instead for off-air Restart).
* Browser Source (`obs-browser`, read at commit `85c7f26`): changing `url`, `css`,
  `shutdown`, `restart_when_active`, `fps`, `reroute_audio` or the local-file flag reloads
  the page; width/height changes only resize it.

## Known limits

* **Browser Source not present in the tested OBS** (Debian/Ubuntu packages are built
  without CEF). Browser-specific behaviour (no reload over 100 cuts, shutdown/refresh flags
  in a live CEF instance) is **untested**. The shared-overlay code paths (one source
  referenced in eight scenes, ordering, visibility, MIXED, Program duplicate, idempotent
  repair) were tested with a stand-in source type (`color_source_v3`) via a self-test-only
  override. Use the OBS PPA / Flatpak / official builds and `tests/fixtures/graphics/` to
  close this gap.
* Rendering and performance were measured with **software** OpenGL under Xvfb on a 4-vCPU
  VM; figures are not representative of a production machine with a GPU.
* Only hard cuts. A shared overlay across a dissolve would double-blend; dissolves/stingers
  need separate design.
* No frame synchronisation / genlock; no signal-loss detection beyond what an input reports
  (status shows "unknown" otherwise).
* Eight 1080p50/60 inputs plus a browser, two loaded media files, encoding and the
  multiview are bounded by the machine and input types; no universal capability is claimed.
* The packaged binary targets OBS 30.2.x on x86_64 Linux. Rebuild for other OBS versions.
* clang-format: sources were formatted with the template's `.clang-format` using
  clang-format **18.1.3**; the template CI pins 19.1.1, which may flag minor differences.
  `gersemi` (CMake formatter) was not available and was not run.
