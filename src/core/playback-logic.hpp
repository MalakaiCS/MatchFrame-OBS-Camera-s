// MatchFrame Multiview - pure playback-slot state machine (no OBS / Qt dependencies).
//
// The PlaybackController feeds observations of the real OBS media source into this
// class and performs the Action it returns. All "should the clip start now?" decisions
// live here so they are unit tested.
#pragma once

#include <cstdint>
#include <string>

namespace mf {

// Mirrors obs_media_state without depending on libobs headers.
enum class MediaState { None, Playing, Opening, Buffering, Paused, Stopped, Ended, Error };

enum class PlaybackState { Empty, Loading, Ready, Playing, Paused, Ended, Error };

const char *playbackStateName(PlaybackState s);

struct MediaObservation {
	MediaState state = MediaState::None;
	int64_t durationMs = 0;
	int64_t timeMs = 0;
	bool hasFrame = false; // the source reports a video size (a frame has been output)
};

enum class PlaybackAction {
	None,
	SetFileAndOpen, // apply file settings to the media source
	PauseSeekStart, // pause and seek to 0 (arming / re-arming)
	Restart,        // start from the beginning (Program entry)
	PlayPause,      // operator transport
};

class PlaybackSlotLogic {
public:
	struct Timing {
		int64_t openTimeoutMs = 8000;
		int64_t settleMs = 200; // paused-at-start must hold this long before READY
		int64_t repauseAfterMs = 1000;
		int maxRepause = 3;
	};

	PlaybackSlotLogic() = default;
	explicit PlaybackSlotLogic(Timing t) : timing_(t) {}

	PlaybackState state() const { return state_; }
	const std::string &file() const { return file_; }
	const std::string &error() const { return error_; }
	bool armed() const { return state_ == PlaybackState::Ready; }
	bool onProgram() const { return onProgram_; }
	bool activeElsewhere() const { return activeElsewhere_; }
	bool hasFile() const { return !file_.empty(); }

	// Reason a load/arm/seek is refused, or empty if allowed.
	static std::string modifyBlockReason(bool onProgram, bool sourceActive);

	// Operator loads (or re-arms) a file. `sourceActive` = media source active anywhere in output.
	PlaybackAction requestLoad(const std::string &path, bool fileExists, bool onProgram, bool sourceActive,
				   int64_t nowMs, std::string *blockReason = nullptr);
	// Re-arm the current file from the beginning (no file change).
	PlaybackAction requestRearm(bool onProgram, bool sourceActive, int64_t nowMs,
				    std::string *blockReason = nullptr);

	// Adopt an existing file without re-applying settings (e.g. collection load: the
	// media source already has the file). Starts arming.
	PlaybackAction adoptExisting(const std::string &path, bool fileExists, bool onProgram, int64_t nowMs);

	// Periodic / signal-driven observation of the real media source.
	PlaybackAction observe(const MediaObservation &obs, int64_t nowMs);

	// Program edges, derived from the actual OBS Program scene.
	PlaybackAction onProgramEnter(int64_t nowMs);
	PlaybackAction onProgramExit(bool sourceStillActive, int64_t nowMs);
	// After an exit while still active elsewhere: the source is no longer active anywhere.
	PlaybackAction onBecameInactive(int64_t nowMs);

	// Operator transport (Play/Pause, Restart) - allowed whenever a file is loaded.
	// Off-air transport disarms the slot (it must be re-armed before a take).
	PlaybackAction transport(PlaybackAction what);

	// The media source disappeared / collection changed.
	void reset();
	void markError(const std::string &why);

private:
	enum class ArmStep { None, WaitOpen, WaitSettle };

	PlaybackAction beginArm(int64_t nowMs, bool alreadyOpen);

	Timing timing_;
	PlaybackState state_ = PlaybackState::Empty;
	std::string file_;
	std::string error_;
	ArmStep arm_ = ArmStep::None;
	int64_t armStartedMs_ = 0;
	int64_t settleSinceMs_ = 0;
	int repauseCount_ = 0;
	bool onProgram_ = false;
	bool activeElsewhere_ = false;
	// libobs applies media actions on its next tick: after a Restart, keep reporting
	// PLAYING until the source confirms it (or this grace period expires).
	int64_t startPendingSinceMs_ = -1;
	static constexpr int64_t kStartGraceMs = 1500;
};

// Formats milliseconds as m:ss (or h:mm:ss).
std::string formatClock(int64_t ms);

} // namespace mf
