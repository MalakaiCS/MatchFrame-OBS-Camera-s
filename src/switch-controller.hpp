// MatchFrame Multiview - Preview / CUT / Return Live using OBS Studio Mode.
#pragma once

#include "core/switch-logic.hpp"
#include "playback-controller.hpp"
#include "scene-bank.hpp"

#include <string>

namespace mf {

struct SwitchView {
	bool studioMode = false;
	bool suspended = false;
	bool busy = false;
	bool transitionActive = false;
	std::string previewName, programName;
	std::string previewUuid, programUuid;
	int previewSlot = -1, programSlot = -1;
	std::string transitionName; // the frontend's selected transition
	bool transitionIsCut = false;
	int lastCameraSlot = -1;
	bool returnLiveEnabled = false;
	std::string returnLiveReason;
	bool cutEnabled = false;
	std::string cutReason;
};

class SwitchController {
public:
	SwitchController(SceneBank &bank, PlaybackController &playback) : bank_(bank), playback_(playback) {}

	// Facts for one slot, resolved now.
	SlotFacts slotFacts(int slot);

	// All return a user-facing message on rejection.
	bool selectPreview(int slot, std::string *message);
	bool cut(std::string *message);
	bool returnLive(std::string *message);

	// Called for frontend events and on the status timer.
	void reconcile();
	void onTransitionFinished() { take_.complete(); }
	void tick() { take_.tick(nowMs()); }

	void setSuspended(bool s);
	bool suspended() const { return suspended_; }

	SwitchView view();
	LastCameraTracker &tracker() { return tracker_; }
	const TakeStateMachine &takeState() const { return take_; }

	// Restores the last-live camera identity from configuration.
	void restoreLastCamera();

private:
	bool ensureCutTransition(std::string *message);
	CutFacts cutFacts();
	ReturnLiveFacts returnLiveFacts(OBSSourceAutoRelease &target);

	SceneBank &bank_;
	PlaybackController &playback_;
	TakeStateMachine take_{2000};
	LastCameraTracker tracker_;
	bool suspended_ = false;
};

} // namespace mf
