// MatchFrame Multiview - pure switching decisions (no OBS / Qt dependencies).
//
// The OBS-facing SwitchController gathers facts from OBS into the structs below,
// asks these functions what to do, and then performs the OBS calls. Keeping the
// decisions here lets them be unit tested without a running OBS.
#pragma once

#include <cstdint>
#include <string>

namespace mf {

constexpr int kCameraCount = 8;
constexpr int kSlotReplay = 8;     // bank slot index 8
constexpr int kSlotHighlights = 9; // bank slot index 9
constexpr int kSlotCount = 10;

inline bool slotIsCamera(int slot)
{
	return slot >= 0 && slot < kCameraCount;
}
inline bool slotIsPlayback(int slot)
{
	return slot == kSlotReplay || slot == kSlotHighlights;
}

// Facts about one bank slot, resolved immediately before a decision.
struct SlotFacts {
	bool sceneExists = false;   // plugin-owned scene resolved by identity
	bool inputAssigned = false; // camera: an input is configured for the slot
	bool inputExists = false;   // camera: the configured input still exists
	bool playbackReady = false; // playback: prepared media is READY (armed)
	bool playbackOnAir = false; // playback: the slot scene is currently Program
};

enum class Reject {
	None,
	StudioModeOff,
	CollectionChanging,
	Busy,             // a plugin take is already in progress
	TransitionActive, // an external transition is still running
	NoPreview,
	SceneMissing,
	Unassigned,
	InputMissing,
	PlaybackNotReady,
	TransitionOverride, // per-scene override would turn CUT into something else
	NoCutTransition,
	NoTarget,         // Return Live: no last camera recorded / it no longer exists
	AlreadyOnProgram, // Return Live target already on Program
};

const char *rejectMessage(Reject r);

// ---------------------------------------------------------------------------
// Preview selection
struct SelectFacts {
	bool studioMode = false;
	bool collectionChanging = false;
	int slot = -1;
	SlotFacts slotFacts;
};

// Returns Reject::None when the slot may be put into OBS Preview.
// Playback slots must be READY before the plugin stages them; a playback scene that
// is already on Program may still be selected (the selection never restarts it).
Reject evaluateSelect(const SelectFacts &f);

// ---------------------------------------------------------------------------
// CUT
struct CutFacts {
	bool studioMode = false;
	bool collectionChanging = false;
	bool busy = false;
	bool transitionActive = false;
	bool cutTransitionAvailable = false;
	bool previewExists = false;
	bool previewEqualsProgram = false;
	bool previewOverrideConflict = false; // preview scene has a non-cut transition override
	int previewSlot = -1;                 // bank slot of the Preview scene, -1 if outside the bank
	SlotFacts previewSlotFacts;
};

enum class CutAction { Execute, NoOp, Reject };

struct CutDecision {
	CutAction action = CutAction::Reject;
	Reject reason = Reject::None;
};

CutDecision evaluateCut(const CutFacts &f);

// ---------------------------------------------------------------------------
// Return Live
struct ReturnLiveFacts {
	bool studioMode = false;
	bool collectionChanging = false;
	bool busy = false;
	bool transitionActive = false;
	bool cutTransitionAvailable = false;
	bool targetKnown = false;       // a last-live camera identity is recorded
	bool targetExists = false;      // it still resolves to a plugin-owned camera scene
	bool targetInputExists = false; // and that camera still has its input
	bool targetOnProgram = false;
	bool targetOverrideConflict = false;
};

CutDecision evaluateReturnLive(const ReturnLiveFacts &f);

// ---------------------------------------------------------------------------
// Take serialization: Idle -> Taking -> Idle, plus Disabled.
// Requests while Taking are dropped (never queued). A take that never reports
// completion is released after a timeout so the controls cannot lock up.
class TakeStateMachine {
public:
	enum class State { Idle, Taking, Disabled };

	explicit TakeStateMachine(int64_t timeoutMs = 2000) : timeoutMs_(timeoutMs) {}

	State state() const { return state_; }
	bool busy() const { return state_ == State::Taking; }

	// Returns true and enters Taking when a take may start now.
	bool tryBegin(int64_t nowMs);
	// OBS reported the transition finished / program changed.
	void complete();
	// Releases a stuck take after the timeout. Returns true if it timed out.
	bool tick(int64_t nowMs);
	void setDisabled(bool disabled);

	uint64_t dropped() const { return dropped_; }

private:
	State state_ = State::Idle;
	int64_t startedMs_ = 0;
	int64_t timeoutMs_;
	uint64_t dropped_ = 0;
};

// ---------------------------------------------------------------------------
// Remembers the last *camera* scene that was actually on Program.
// Only Program observations update it - never Preview, never playback scenes,
// never scenes outside the bank.
class LastCameraTracker {
public:
	// `programSlot` is the bank slot of the current Program scene (-1 if outside bank).
	void observeProgram(int programSlot, const std::string &programSceneId);
	const std::string &lastCameraId() const { return lastId_; }
	int lastCameraSlot() const { return lastSlot_; }
	void reset(const std::string &id = std::string(), int slot = -1)
	{
		lastId_ = id;
		lastSlot_ = id.empty() ? -1 : slot;
	}

private:
	std::string lastId_;
	int lastSlot_ = -1;
};

} // namespace mf
