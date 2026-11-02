// MatchFrame Multiview - small libobs helpers shared by the controllers.
#pragma once

#include <obs.h>
#include <obs.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace mf {

// Ownership markers written into an object's private settings (persisted by OBS in
// the scene collection). A matching *name* is never treated as proof of ownership.
constexpr const char *kOwnerKey = "mf_owner";
constexpr const char *kOwnerValue = "matchframe-multiview";
constexpr const char *kBankKey = "mf_bank";
constexpr const char *kRoleKey = "mf_role";

std::string uuidOf(obs_source_t *source);
std::string nameOf(obs_source_t *source);

void markOwned(obs_source_t *source, const std::string &bankId, const std::string &role);
bool isOwned(obs_source_t *source, const std::string &bankId, const std::string &role);
bool isOwnedByAnyBank(obs_source_t *source);

OBSSourceAutoRelease sourceByUuid(const std::string &uuid);

// Searches scenes (scenes=true) or inputs for an object carrying this bank's role marker.
OBSSourceAutoRelease findOwned(const std::string &bankId, const std::string &role, bool scenes);

// Returns `base` if unused, otherwise "base (2)", "base (3)", ...
std::string uniqueSourceName(const std::string &base);

void canvasSize(uint32_t &w, uint32_t &h);
double canvasAspect();

// Items of `scene` in bottom-to-top order (pointers valid while the scene is held).
std::vector<obs_sceneitem_t *> sceneItems(obs_scene_t *scene);
int itemIndex(obs_scene_t *scene, obs_sceneitem_t *item); // -1 if absent
// Finds the item with this id whose source is `expect` (nullptr if id stale / mismatched).
obs_sceneitem_t *itemByIdAndSource(obs_scene_t *scene, int64_t id, obs_source_t *expect);
std::vector<obs_sceneitem_t *> itemsWithSource(obs_scene_t *scene, obs_source_t *source);

bool sourceTypeAvailable(const char *id);

// Source type used for graphics overlays: always "browser_source" in release builds.
// Self-test builds may substitute a stand-in type via MATCHFRAME_TEST_GRAPHICS_SOURCE
// (used where OBS has no Browser Source, e.g. distro builds without CEF).
const char *graphicsSourceTypeId();

// Returns the frontend's "cut_transition" instance (retained) or nullptr.
OBSSourceAutoRelease findCutTransition();
OBSSourceAutoRelease findTransitionByName(const std::string &name);

// Per-scene transition: OBS's own per-scene "Transition Override", stored in the scene's
// private settings. OBS applies it whenever that scene is transitioned to Program (the
// plugin's CUT / Return Live, OBS's Transition button, hotkeys). Empty name = none, so
// the plugin's default Cut is used.
std::string sceneTransitionOverride(obs_source_t *scene);
int sceneTransitionDuration(obs_source_t *scene); // ms; OBS's default when unset
void setSceneTransition(obs_source_t *scene, const std::string &name, int durationMs);
void clearTransitionOverride(obs_source_t *scene);
// The frontend's transitions, by display name (includes Cut).
std::vector<std::string> transitionNames();
// True for transitions without an adjustable duration (Cut, Stinger).
bool transitionIsFixed(obs_source_t *transition);
bool transitionIsCutType(obs_source_t *transition);
// "Cut", "Fade 300 ms", "Stinger" ... for the scene's effective transition when taken.
std::string describeSceneTransition(obs_source_t *scene);

// True while the program transition is mid-way (0 < t < 1).
bool transitionInProgress();

// OBS Studio Mode preferences (read-only).
bool studioSceneDuplicationEnabled();
bool studioDuplicateSourcesEnabled(); // "EditPropertiesMode": duplicates sources on take
bool studioSwapScenesEnabled();

int64_t nowMs();

} // namespace mf
