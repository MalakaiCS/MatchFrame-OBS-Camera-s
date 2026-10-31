#include "switch-logic.hpp"

namespace mf {

const char *rejectMessage(Reject r)
{
	switch (r) {
	case Reject::None:
		return "";
	case Reject::StudioModeOff:
		return "Studio Mode is off - enable it to use Preview / CUT";
	case Reject::CollectionChanging:
		return "Scene collection is changing - switching suspended";
	case Reject::Busy:
		return "A take is already in progress";
	case Reject::TransitionActive:
		return "A transition is still running in OBS - wait for it to finish";
	case Reject::NoPreview:
		return "Nothing is in Preview";
	case Reject::SceneMissing:
		return "The MatchFrame scene for this slot is missing - run Create / Repair";
	case Reject::Unassigned:
		return "This camera slot is unassigned";
	case Reject::InputMissing:
		return "The assigned camera input no longer exists";
	case Reject::PlaybackNotReady:
		return "Playback is not READY - load and arm a file first";
	case Reject::NoCutTransition:
		return "OBS has no Cut transition available";
	case Reject::NoTarget:
		return "No valid last live camera to return to";
	case Reject::AlreadyOnProgram:
		return "The last live camera is already on Program";
	}
	return "";
}

static Reject checkSlot(int slot, const SlotFacts &s, bool forTake)
{
	if (!s.sceneExists)
		return Reject::SceneMissing;
	if (slotIsCamera(slot)) {
		if (!s.inputAssigned)
			return Reject::Unassigned;
		if (!s.inputExists)
			return Reject::InputMissing;
		return Reject::None;
	}
	if (slotIsPlayback(slot)) {
		// A playback scene already on Program is a valid selection (no restart),
		// but taking it again is a no-op handled by the caller.
		if (s.playbackOnAir && !forTake)
			return Reject::None;
		if (!s.playbackReady)
			return Reject::PlaybackNotReady;
	}
	return Reject::None;
}

Reject evaluateSelect(const SelectFacts &f)
{
	if (f.collectionChanging)
		return Reject::CollectionChanging;
	if (!f.studioMode)
		return Reject::StudioModeOff;
	if (f.slot < 0 || f.slot >= kSlotCount)
		return Reject::SceneMissing;
	return checkSlot(f.slot, f.slotFacts, false);
}

CutDecision evaluateCut(const CutFacts &f)
{
	CutDecision d;
	auto reject = [&](Reject r) {
		d.action = CutAction::Reject;
		d.reason = r;
		return d;
	};
	if (f.collectionChanging)
		return reject(Reject::CollectionChanging);
	if (!f.studioMode)
		return reject(Reject::StudioModeOff);
	if (f.busy)
		return reject(Reject::Busy);
	if (f.transitionActive)
		return reject(Reject::TransitionActive);
	if (!f.previewExists)
		return reject(Reject::NoPreview);
	if (f.previewEqualsProgram) {
		// Same scene in Preview and Program: nothing to do, and never restart media.
		d.action = CutAction::NoOp;
		d.reason = Reject::None;
		return d;
	}
	if (f.previewSlot >= 0) {
		Reject r = checkSlot(f.previewSlot, f.previewSlotFacts, true);
		if (r != Reject::None)
			return reject(r);
	}
	if (!f.cutTransitionAvailable)
		return reject(Reject::NoCutTransition);
	d.action = CutAction::Execute;
	d.reason = Reject::None;
	return d;
}

CutDecision evaluateReturnLive(const ReturnLiveFacts &f)
{
	CutDecision d;
	auto reject = [&](Reject r) {
		d.action = CutAction::Reject;
		d.reason = r;
		return d;
	};
	if (f.collectionChanging)
		return reject(Reject::CollectionChanging);
	if (!f.studioMode)
		return reject(Reject::StudioModeOff);
	if (f.busy)
		return reject(Reject::Busy);
	if (f.transitionActive)
		return reject(Reject::TransitionActive);
	if (!f.targetKnown || !f.targetExists)
		return reject(Reject::NoTarget);
	if (!f.targetInputExists)
		return reject(Reject::InputMissing);
	if (f.targetOnProgram)
		return reject(Reject::AlreadyOnProgram);
	if (!f.cutTransitionAvailable)
		return reject(Reject::NoCutTransition);
	d.action = CutAction::Execute;
	return d;
}

bool TakeStateMachine::tryBegin(int64_t nowMs)
{
	if (state_ != State::Idle) {
		dropped_++;
		return false;
	}
	state_ = State::Taking;
	startedMs_ = nowMs;
	return true;
}

void TakeStateMachine::complete()
{
	if (state_ == State::Taking)
		state_ = State::Idle;
}

bool TakeStateMachine::tick(int64_t nowMs)
{
	if (state_ == State::Taking && nowMs - startedMs_ >= timeoutMs_) {
		state_ = State::Idle;
		return true;
	}
	return false;
}

void TakeStateMachine::setDisabled(bool disabled)
{
	if (disabled)
		state_ = State::Disabled;
	else if (state_ == State::Disabled)
		state_ = State::Idle;
}

void LastCameraTracker::observeProgram(int programSlot, const std::string &programSceneId)
{
	if (slotIsCamera(programSlot) && !programSceneId.empty()) {
		lastId_ = programSceneId;
		lastSlot_ = programSlot;
	}
	// Playback scenes and scenes outside the bank leave the last camera unchanged.
}

} // namespace mf
