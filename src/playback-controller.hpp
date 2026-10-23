// MatchFrame Multiview - Replay / Highlights prepared-media control.
#pragma once

#include "core/playback-logic.hpp"
#include "scene-bank.hpp"

#include <QObject>
#include <QPointer>

#include <functional>
#include <string>

namespace mf {

struct PlaybackView {
	PlaybackState state = PlaybackState::Empty;
	std::string file;     // full path
	std::string fileName; // display name
	std::string error;
	int64_t timeMs = 0;
	int64_t durationMs = 0;
	bool onProgram = false;
	bool activeElsewhere = false;
	bool sourceExists = false;
	bool canModify = false; // load / arm / seek allowed (off-air and inactive)
	std::string modifyBlockReason;
};

class PlaybackController {
public:
	explicit PlaybackController(SceneBank &bank);
	~PlaybackController();

	// Called on the UI thread (via `context`) when a media lifecycle signal fired.
	// Queued calls are dropped automatically if `context` is destroyed first.
	void setWakeCallback(QObject *context, std::function<void()> cb)
	{
		context_ = context;
		wake_ = std::move(cb);
	}

	PlaybackView view(int slot);
	bool readyForTake(int slot) const;

	bool load(int slot, const std::string &path, std::string *message);
	bool rearm(int slot, std::string *message);
	bool playPause(int slot, std::string *message);
	bool restart(int slot, std::string *message);

	// Reconcile with the actual Program scene. Safe to call at any rate: it only acts
	// on Program edges and arming steps, never because something was rendered.
	void poll(int programSlot);

	void resume(int programSlot); // after a collection load: adopt the saved files
	void suspend();               // collection change / shutdown: drop all references

	PlaybackSlotLogic &logic(int slot) { return logic_[slot == kSlotReplay ? 0 : 1]; }
	const PlaybackSlotLogic &logic(int slot) const { return logic_[slot == kSlotReplay ? 0 : 1]; }

private:
	void perform(int slot, PlaybackAction action, obs_source_t *media);
	void reloadFile(obs_source_t *media, const std::string &path);
	void connectSignals(int slot, obs_source_t *media);
	void disconnectSignals(int slot);
	static void onMediaSignal(void *data, calldata_t *cd);

	SceneBank &bank_;
	PlaybackSlotLogic logic_[2];
	OBSWeakSourceAutoRelease connected_[2];
	QPointer<QObject> context_;
	std::function<void()> wake_;
};

MediaState toMediaState(enum obs_media_state s);

} // namespace mf
