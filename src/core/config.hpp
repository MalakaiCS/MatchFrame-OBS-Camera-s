// MatchFrame Multiview - per-scene-collection configuration model.
//
// Stored through OBS's frontend save callback (inside the scene collection file).
// Serialization uses only the libobs obs_data API, which works without a running OBS,
// so it is unit tested directly.
#pragma once

#include <array>
#include <cstdint>
#include <string>

struct obs_data;
typedef struct obs_data obs_data_t;

namespace mf {

constexpr int kConfigVersion = 2;
constexpr const char *kSaveKey = "matchframe_multiview";

enum class Framing { Fit, Fill };

struct CameraSlotConfig {
	std::string label;
	std::string sceneUuid;      // plugin-owned scene (identity)
	std::string sceneName;      // last known display name (informational / fallback)
	std::string inputUuid;      // assigned existing OBS input
	std::string inputName;      // last known input name (controlled fallback only)
	std::string inputTypeId;    // input type id, required to match for name fallback
	int64_t inputItemId = 0;    // scene item id of the camera input in the camera scene
	int64_t graphicsItemId = 0; // scene item id of the shared graphics browser in this scene
};

struct PlaybackSlotConfig {
	std::string label;
	std::string sceneUuid;
	std::string sceneName;
	std::string mediaUuid; // plugin-owned ffmpeg media source
	int64_t mediaItemId = 0;
	std::string filePath;
	std::string graphicsUrl; // optional playback overlay (independent of camera graphics)
	bool graphicsEnabled = false;
	std::string graphicsUuid;
	int64_t graphicsItemId = 0;
};

struct BankConfig {
	int version = kConfigVersion;
	std::string bankId; // random id marking objects owned by this bank
	std::array<CameraSlotConfig, 8> cameras;
	PlaybackSlotConfig replay;
	PlaybackSlotConfig highlights;
	std::string graphicsUrl;  // shared camera graphics output URL (operator supplied)
	std::string graphicsUuid; // the one shared camera-graphics Browser Source
	bool graphicsVisible = true;
	std::string lastLiveCameraUuid;
	// Replay / Highlights provider: "builtin" (prepared files in MF — REPLAY / MF — HIGHLIGHTS)
	// or "replaycue" (drive the MatchFrame Replay Cue plugin and show its scenes).
	std::string playbackProvider = "builtin";
	std::string replayCueReplayScene;     // empty = auto-detect
	std::string replayCueHighlightsScene; // empty = auto-detect
	bool migratedFromV1 = false;          // set by load when an old schema was upgraded

	PlaybackSlotConfig &playback(int slot) { return slot == 8 ? replay : highlights; }
	const PlaybackSlotConfig &playback(int slot) const { return slot == 8 ? replay : highlights; }
};

// Default names and labels from the production brief.
const char *defaultCameraSceneName(int cam); // "MF — CAM 01".."MF — CAM 08"
const char *defaultCameraLabel(int cam);
const char *defaultPlaybackSceneName(int slot); // slot 8 / 9
const char *defaultPlaybackLabel(int slot);

BankConfig makeDefaultConfig(const std::string &bankId);

// Serialize into / read from an obs_data object (the caller owns `data`).
void saveConfig(const BankConfig &cfg, obs_data_t *data);
// Missing / empty data yields defaults with `bankId` = fallbackBankId. Older schemas are migrated.
BankConfig loadConfig(obs_data_t *data, const std::string &fallbackBankId);

// Removes query string, fragment and user-info so tokenized URLs never reach logs.
std::string redactUrl(const std::string &url);

// Returns a short random hex id (used for bank ids).
std::string randomId();

} // namespace mf
