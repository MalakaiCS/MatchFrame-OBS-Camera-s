#include "playback-logic.hpp"

#include <cstdio>

namespace mf {

const char *playbackStateName(PlaybackState s)
{
	switch (s) {
	case PlaybackState::Empty:
		return "EMPTY";
	case PlaybackState::Loading:
		return "LOADING";
	case PlaybackState::Ready:
		return "READY";
	case PlaybackState::Playing:
		return "PLAYING";
	case PlaybackState::Paused:
		return "PAUSED";
	case PlaybackState::Ended:
		return "ENDED";
	case PlaybackState::Error:
		return "ERROR";
	}
	return "?";
}

std::string formatClock(int64_t ms)
{
	if (ms < 0)
		ms = 0;
	int64_t s = ms / 1000;
	char buf[32];
	if (s >= 3600)
		snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", (long long)(s / 3600), (long long)((s / 60) % 60),
			 (long long)(s % 60));
	else
		snprintf(buf, sizeof(buf), "%lld:%02lld", (long long)(s / 60), (long long)(s % 60));
	return buf;
}

std::string PlaybackSlotLogic::modifyBlockReason(bool onProgram, bool sourceActive)
{
	if (onProgram)
		return "This playback scene is on Program - take another scene first";
	if (sourceActive)
		return "This media source is active in the output elsewhere - remove it from Program first";
	return std::string();
}

PlaybackAction PlaybackSlotLogic::beginArm(int64_t nowMs, bool alreadyOpen)
{
	state_ = PlaybackState::Loading;
	error_.clear();
	armStartedMs_ = nowMs;
	repauseCount_ = 0;
	if (alreadyOpen) {
		arm_ = ArmStep::WaitSettle;
		settleSinceMs_ = nowMs;
		return PlaybackAction::PauseSeekStart;
	}
	arm_ = ArmStep::WaitOpen;
	return PlaybackAction::SetFileAndOpen;
}

PlaybackAction PlaybackSlotLogic::requestLoad(const std::string &path, bool fileExists, bool onProgram,
					      bool sourceActive, int64_t nowMs, std::string *blockReason)
{
	std::string why = modifyBlockReason(onProgram, sourceActive);
	if (!why.empty()) {
		if (blockReason)
			*blockReason = why;
		return PlaybackAction::None;
	}
	if (path.empty()) {
		if (blockReason)
			*blockReason = "No file selected";
		return PlaybackAction::None;
	}
	file_ = path;
	if (!fileExists) {
		markError("File not found");
		if (blockReason)
			*blockReason = "File not found";
		return PlaybackAction::None;
	}
	return beginArm(nowMs, false);
}

PlaybackAction PlaybackSlotLogic::requestRearm(bool onProgram, bool sourceActive, int64_t nowMs,
					       std::string *blockReason)
{
	std::string why = modifyBlockReason(onProgram, sourceActive);
	if (!why.empty()) {
		if (blockReason)
			*blockReason = why;
		return PlaybackAction::None;
	}
	if (file_.empty()) {
		if (blockReason)
			*blockReason = "No file loaded";
		return PlaybackAction::None;
	}
	if (state_ == PlaybackState::Error)
		return beginArm(nowMs, false); // reopen
	return beginArm(nowMs, true);
}

PlaybackAction PlaybackSlotLogic::adoptExisting(const std::string &path, bool fileExists, bool onProgram, int64_t nowMs)
{
	file_ = path;
	onProgram_ = onProgram;
	if (path.empty()) {
		state_ = PlaybackState::Empty;
		return PlaybackAction::None;
	}
	if (!fileExists) {
		markError("File not found");
		return PlaybackAction::None;
	}
	if (onProgram) {
		// Already live (e.g. collection loaded with this scene on Program): report truthfully.
		state_ = PlaybackState::Playing;
		arm_ = ArmStep::None;
		return PlaybackAction::None;
	}
	// The media source was created from saved settings and may already be running;
	// wait for it to open, then pause at the start.
	state_ = PlaybackState::Loading;
	arm_ = ArmStep::WaitOpen;
	armStartedMs_ = nowMs;
	repauseCount_ = 0;
	return PlaybackAction::None;
}

static bool isOpen(MediaState s)
{
	return s == MediaState::Playing || s == MediaState::Paused || s == MediaState::Ended ||
	       s == MediaState::Stopped;
}

PlaybackAction PlaybackSlotLogic::observe(const MediaObservation &obs, int64_t nowMs)
{
	if (state_ == PlaybackState::Empty)
		return PlaybackAction::None;

	if (onProgram_) {
		if (startPendingSinceMs_ >= 0) {
			if (obs.state == MediaState::Playing || nowMs - startPendingSinceMs_ > kStartGraceMs)
				startPendingSinceMs_ = -1;
			else
				return PlaybackAction::None; // restart requested, not applied by OBS yet
		}
		// While live, report what the media source is actually doing.
		switch (obs.state) {
		case MediaState::Playing:
		case MediaState::Opening:
		case MediaState::Buffering:
			state_ = PlaybackState::Playing;
			break;
		case MediaState::Paused:
			state_ = PlaybackState::Paused;
			break;
		case MediaState::Ended:
		case MediaState::Stopped:
			state_ = PlaybackState::Ended;
			break;
		case MediaState::Error:
			markError("Media error");
			break;
		case MediaState::None:
			break;
		}
		return PlaybackAction::None;
	}

	// Off-air errors stay visible until the operator loads / re-arms again.
	if (state_ == PlaybackState::Error)
		return PlaybackAction::None;

	if (state_ == PlaybackState::Loading) {
		if (obs.state == MediaState::Error) {
			markError("Media failed to open");
			return PlaybackAction::None;
		}
		if (nowMs - armStartedMs_ > timing_.openTimeoutMs) {
			markError("Media did not become ready (unsupported or unreadable file?)");
			return PlaybackAction::None;
		}
		if (arm_ == ArmStep::WaitOpen) {
			if (isOpen(obs.state) && obs.durationMs > 0) {
				arm_ = ArmStep::WaitSettle;
				settleSinceMs_ = nowMs;
				return PlaybackAction::PauseSeekStart;
			}
			return PlaybackAction::None;
		}
		if (arm_ == ArmStep::WaitSettle) {
			if (obs.state == MediaState::Paused && obs.hasFrame && obs.durationMs > 0) {
				if (nowMs - settleSinceMs_ >= timing_.settleMs) {
					state_ = PlaybackState::Ready;
					arm_ = ArmStep::None;
				}
				return PlaybackAction::None;
			}
			if (obs.state != MediaState::Paused && nowMs - settleSinceMs_ >= timing_.repauseAfterMs) {
				if (repauseCount_ >= timing_.maxRepause) {
					markError("Media could not be paused at the first frame");
					return PlaybackAction::None;
				}
				repauseCount_++;
				settleSinceMs_ = nowMs;
				return PlaybackAction::PauseSeekStart;
			}
		}
		return PlaybackAction::None;
	}

	// Off-air, not arming: report truthfully, but a READY slot stays READY only while
	// the source is still paused (someone pressing play in OBS disarms it).
	switch (obs.state) {
	case MediaState::Playing:
		state_ = PlaybackState::Playing;
		break;
	case MediaState::Paused:
		if (state_ != PlaybackState::Ready)
			state_ = PlaybackState::Paused;
		break;
	case MediaState::Ended:
	case MediaState::Stopped:
		state_ = PlaybackState::Ended;
		break;
	case MediaState::Error:
		markError("Media error");
		break;
	default:
		break;
	}
	return PlaybackAction::None;
}

PlaybackAction PlaybackSlotLogic::onProgramEnter(int64_t nowMs)
{
	if (onProgram_)
		return PlaybackAction::None; // already live: never restart
	onProgram_ = true;
	activeElsewhere_ = false;
	arm_ = ArmStep::None;
	if (file_.empty() || state_ == PlaybackState::Empty || state_ == PlaybackState::Error)
		return PlaybackAction::None;
	// Start from the beginning on actual Program entry (plugin CUT or native OBS take).
	state_ = PlaybackState::Playing;
	startPendingSinceMs_ = nowMs;
	return PlaybackAction::Restart;
}

PlaybackAction PlaybackSlotLogic::onProgramExit(bool sourceStillActive, int64_t nowMs)
{
	if (!onProgram_)
		return PlaybackAction::None;
	onProgram_ = false;
	startPendingSinceMs_ = -1;
	if (sourceStillActive) {
		// Still used elsewhere in the output: do not stop that use.
		activeElsewhere_ = true;
		return PlaybackAction::None;
	}
	activeElsewhere_ = false;
	if (file_.empty() || state_ == PlaybackState::Error || state_ == PlaybackState::Empty)
		return PlaybackAction::None;
	// Pause immediately and re-arm from the beginning for the next take.
	return beginArm(nowMs, true);
}

PlaybackAction PlaybackSlotLogic::onBecameInactive(int64_t nowMs)
{
	if (!activeElsewhere_ || onProgram_)
		return PlaybackAction::None;
	activeElsewhere_ = false;
	if (file_.empty() || state_ == PlaybackState::Error || state_ == PlaybackState::Empty)
		return PlaybackAction::None;
	return beginArm(nowMs, true);
}

PlaybackAction PlaybackSlotLogic::transport(PlaybackAction what)
{
	if (file_.empty() || state_ == PlaybackState::Empty || state_ == PlaybackState::Error)
		return PlaybackAction::None;
	if (what != PlaybackAction::PlayPause && what != PlaybackAction::Restart)
		return PlaybackAction::None;
	startPendingSinceMs_ = -1;
	if (!onProgram_) {
		// Off-air transport: the slot is no longer at its armed first frame.
		arm_ = ArmStep::None;
		if (state_ == PlaybackState::Ready || state_ == PlaybackState::Loading)
			state_ = PlaybackState::Paused;
	}
	return what;
}

void PlaybackSlotLogic::reset()
{
	state_ = PlaybackState::Empty;
	file_.clear();
	error_.clear();
	arm_ = ArmStep::None;
	onProgram_ = false;
	activeElsewhere_ = false;
	startPendingSinceMs_ = -1;
}

void PlaybackSlotLogic::markError(const std::string &why)
{
	state_ = PlaybackState::Error;
	error_ = why;
	arm_ = ArmStep::None;
}

} // namespace mf
