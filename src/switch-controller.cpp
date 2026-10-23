#include "switch-controller.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <cstring>

namespace mf {

SlotFacts SwitchController::slotFacts(int slot)
{
	SlotFacts f;
	OBSSourceAutoRelease scene = bank_.slotScene(slot);
	f.sceneExists = scene != nullptr;
	if (slotIsCamera(slot)) {
		const CameraSlotConfig &c = bank_.config().cameras[slot];
		f.inputAssigned = !c.inputUuid.empty() || !c.inputName.empty();
		OBSSourceAutoRelease input = bank_.cameraInput(slot);
		f.inputExists = input != nullptr;
	} else if (slotIsPlayback(slot)) {
		f.playbackReady = playback_.readyForTake(slot);
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		f.playbackOnAir = scene && program && program.Get() == scene.Get();
	}
	return f;
}

bool SwitchController::selectPreview(int slot, std::string *message)
{
	SelectFacts f;
	f.studioMode = obs_frontend_preview_program_mode_active();
	f.collectionChanging = suspended_;
	f.slot = slot;
	f.slotFacts = slotFacts(slot);
	Reject r = evaluateSelect(f);
	if (r != Reject::None) {
		if (message)
			*message = rejectMessage(r);
		return false;
	}
	OBSSourceAutoRelease scene = bank_.slotScene(slot);
	obs_frontend_set_current_preview_scene(scene);
	return true;
}

bool SwitchController::ensureCutTransition(std::string *message)
{
	OBSSourceAutoRelease cut = findCutTransition();
	if (!cut) {
		if (message)
			*message = rejectMessage(Reject::NoCutTransition);
		return false;
	}
	OBSSourceAutoRelease current = obs_frontend_get_current_transition();
	if (current.Get() != cut.Get()) {
		obs_log(LOG_INFO, "selecting the Cut transition for MatchFrame takes (was '%s')",
			nameOf(current).c_str());
		obs_frontend_set_current_transition(cut);
		OBSSourceAutoRelease after = obs_frontend_get_current_transition();
		if (after.Get() != cut.Get()) {
			if (message)
				*message = "Could not select the Cut transition";
			return false;
		}
	}
	return true;
}

CutFacts SwitchController::cutFacts()
{
	CutFacts f;
	f.studioMode = obs_frontend_preview_program_mode_active();
	f.collectionChanging = suspended_;
	f.busy = take_.busy();
	f.transitionActive = transitionInProgress();
	OBSSourceAutoRelease cut = findCutTransition();
	f.cutTransitionAvailable = cut != nullptr;
	OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	f.previewExists = preview != nullptr;
	f.previewEqualsProgram = preview && program && preview.Get() == program.Get();
	if (preview) {
		f.previewOverrideConflict = overrideConflicts(preview);
		f.previewSlot = bank_.slotOfScene(preview);
		if (f.previewSlot >= 0)
			f.previewSlotFacts = slotFacts(f.previewSlot);
	}
	return f;
}

bool SwitchController::cut(std::string *message)
{
	take_.tick(nowMs());
	CutFacts f = cutFacts();
	CutDecision d = evaluateCut(f);
	if (d.action == CutAction::NoOp) {
		if (message)
			*message = "Preview is already on Program - nothing to cut";
		return true;
	}
	if (d.action == CutAction::Reject) {
		if (message) {
			*message = rejectMessage(d.reason);
			if (d.reason == Reject::TransitionOverride) {
				OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
				std::string name;
				overrideConflicts(preview, &name);
				*message += " ('" + nameOf(preview) + "' overrides with '" + name + "')";
			}
		}
		return false;
	}
	if (!ensureCutTransition(message))
		return false;
	if (!take_.tryBegin(nowMs())) {
		if (message)
			*message = rejectMessage(Reject::Busy);
		return false;
	}
	OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
	obs_log(LOG_INFO, "CUT: '%s' -> Program", nameOf(preview).c_str());
	obs_frontend_preview_program_trigger_transition();
	reconcile();
	return true;
}

ReturnLiveFacts SwitchController::returnLiveFacts(OBSSourceAutoRelease &target)
{
	ReturnLiveFacts f;
	f.studioMode = obs_frontend_preview_program_mode_active();
	f.collectionChanging = suspended_;
	f.busy = take_.busy();
	f.transitionActive = transitionInProgress();
	OBSSourceAutoRelease cut = findCutTransition();
	f.cutTransitionAvailable = cut != nullptr;
	const std::string &id = tracker_.lastCameraId();
	f.targetKnown = !id.empty();
	int slot = bank_.slotOfSceneUuid(id);
	if (f.targetKnown && slotIsCamera(slot)) {
		target = bank_.slotScene(slot);
		// The scene must still be the same object (identity), not a re-created namesake.
		f.targetExists = target && uuidOf(target) == id;
		OBSSourceAutoRelease input = bank_.cameraInput(slot);
		f.targetInputExists = input != nullptr;
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		f.targetOnProgram = target && program && target.Get() == program.Get();
		f.targetOverrideConflict = target && overrideConflicts(target);
	}
	return f;
}

bool SwitchController::returnLive(std::string *message)
{
	take_.tick(nowMs());
	OBSSourceAutoRelease target;
	ReturnLiveFacts f = returnLiveFacts(target);
	CutDecision d = evaluateReturnLive(f);
	if (d.action != CutAction::Execute) {
		if (message)
			*message = rejectMessage(d.reason);
		return false;
	}
	if (!ensureCutTransition(message))
		return false;
	if (!take_.tryBegin(nowMs())) {
		if (message)
			*message = rejectMessage(Reject::Busy);
		return false;
	}
	obs_log(LOG_INFO, "RETURN LIVE: '%s' -> Program", nameOf(target).c_str());
	// In Studio Mode this transitions the given scene straight to Program without
	// changing what the operator has staged in Preview.
	obs_frontend_set_current_scene(target);
	reconcile();
	return true;
}

void SwitchController::reconcile()
{
	if (suspended_)
		return;
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	int slot = bank_.slotOfScene(program);
	tracker_.observeProgram(slot, uuidOf(program));
	bank_.config().lastLiveCameraUuid = tracker_.lastCameraId();
	playback_.poll(slot);
}

void SwitchController::setSuspended(bool s)
{
	suspended_ = s;
	take_.setDisabled(s);
}

void SwitchController::restoreLastCamera()
{
	const std::string &id = bank_.config().lastLiveCameraUuid;
	tracker_.reset(id, bank_.slotOfSceneUuid(id));
}

SwitchView SwitchController::view()
{
	SwitchView v;
	v.studioMode = obs_frontend_preview_program_mode_active();
	v.suspended = suspended_;
	v.busy = take_.busy();
	v.transitionActive = transitionInProgress();
	OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	v.previewName = nameOf(preview);
	v.programName = nameOf(program);
	v.previewUuid = uuidOf(preview);
	v.programUuid = uuidOf(program);
	v.previewSlot = bank_.slotOfScene(preview);
	v.programSlot = bank_.slotOfScene(program);
	OBSSourceAutoRelease tr = obs_frontend_get_current_transition();
	v.transitionName = nameOf(tr);
	v.transitionIsCut = tr && strcmp(obs_source_get_unversioned_id(tr), "cut_transition") == 0;
	v.lastCameraSlot = bank_.slotOfSceneUuid(tracker_.lastCameraId());

	CutFacts cf = cutFacts();
	CutDecision cd = evaluateCut(cf);
	v.cutEnabled = cd.action == CutAction::Execute;
	v.cutReason = cd.action == CutAction::NoOp ? "Preview is already on Program" : rejectMessage(cd.reason);

	OBSSourceAutoRelease target;
	ReturnLiveFacts rf = returnLiveFacts(target);
	CutDecision rd = evaluateReturnLive(rf);
	v.returnLiveEnabled = rd.action == CutAction::Execute;
	v.returnLiveReason = rejectMessage(rd.reason);
	return v;
}

} // namespace mf
