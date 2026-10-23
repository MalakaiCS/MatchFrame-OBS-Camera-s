// MatchFrame Multiview - MatchFrame Replay Cue status model (no OBS runtime / Qt needed).
//
// Replay Cue (github.com/MalakaiCS/MatchFrame-OBS-Replay-Cue) publishes an obs-websocket
// vendor named "ReplayCue". This file turns its GetStatus / GetEvents responses into a
// plain struct and picks which of its scenes the Replay / Highlights tiles show.
#pragma once

#include <string>
#include <utility>
#include <vector>

struct obs_data;
typedef struct obs_data obs_data_t;

namespace mf {

constexpr const char *kReplayCueVendor = "ReplayCue";
constexpr const char *kReplayCuePlayerId = "replay_cue_player";
constexpr const char *kReplayCueDefaultScene = "Replay";

struct ReplayCueStatus {
	bool ok = false;    // a status response was received
	std::string error;  // Replay Cue's or obs-websocket's error text
	bool armed = false; // per-camera buffers recording
	int pendingReplays = 0;
	std::string selectedEvent;
	// playback
	std::string state = "idle"; // idle / loading / playing / paused / ended / error
	std::string eventId;
	std::string eventName;
	int angle = 0; // 1-based, 0 = none
	int angleCount = 0;
	std::string angleLabel;
	double speed = 1.0;
	double position = 0;
	double duration = 0;
	bool cued = false;       // loaded paused, waiting to go live
	bool highlights = false; // a highlights reel is playing
	// match
	std::string matchTitle;
	std::string phaseName;
	bool inBreak = false;
	int eventCount = -1; // from GetEvents (-1 = unknown)
};

// GetStatus responseData -> status. A null / failed response yields ok=false.
ReplayCueStatus parseReplayCueStatus(obs_data_t *status);
// GetEvents responseData -> number of replays in the list (-1 if unavailable).
int parseReplayCueEventCount(obs_data_t *events);

// One-line tile text, e.g. "PLAYING Goal 1 · Cam 2 · 50% · 0:03/0:08".
std::string describeReplayCuePlayback(const ReplayCueStatus &s);

struct SceneCandidate {
	std::string name;
	bool hasPlayer = false; // contains a Replay Cue Player source
};

// Chooses {replay scene, highlights scene}. Configured names win when they exist;
// otherwise "Replay", then the scenes that contain Replay Cue's player. The
// highlights scene falls back to the replay scene (Replay Cue's own default).
// Empty strings mean "not found yet" (Replay Cue creates its scene on first play).
std::pair<std::string, std::string> pickReplayCueScenes(const std::vector<SceneCandidate> &scenes,
							const std::string &configuredReplay,
							const std::string &configuredHighlights);

} // namespace mf
