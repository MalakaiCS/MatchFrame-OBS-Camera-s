#include "controller.hpp"

#include "clean-feed.hpp"
#include "multiview-window.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <QMainWindow>
#include <QMetaObject>

namespace mf {

static Controller *g_instance = nullptr;

Controller *Controller::instance()
{
	return g_instance;
}

Controller::Controller()
{
	g_instance = this;
	hotkeys_.fill(OBS_INVALID_HOTKEY_ID);
	cfg_ = makeDefaultConfig(randomId());
	playback_.setWakeCallback(this, [this]() {
		if (collectionReady()) {
			switch_.reconcile();
			emit stateChanged();
		}
	});
	timer_.setInterval(100);
	connect(&timer_, &QTimer::timeout, this, &Controller::onTimer);
	timer_.start();
}

Controller::~Controller()
{
	shutdown();
	unregisterHotkeys();
	if (g_instance == this)
		g_instance = nullptr;
}

void Controller::setMessage(const QString &msg, bool error)
{
	message_ = msg;
	messageError_ = error;
	if (!msg.isEmpty())
		obs_log(error ? LOG_WARNING : LOG_INFO, "%s", msg.toUtf8().constData());
	emit messageChanged(msg, error);
}

bool Controller::outputsStreaming() const
{
	return obs_frontend_streaming_active();
}

bool Controller::outputsRecording() const
{
	return obs_frontend_recording_active();
}

QString Controller::slotLabel(int slot) const
{
	if (slotIsCamera(slot))
		return QString::fromStdString(cfg_.cameras[slot].label);
	if (slot >= kCameraCount && slot < kSlotCount)
		return QString::fromStdString(cfg_.playback(slot).label);
	return QString();
}

void Controller::setSlotLabel(int slot, const QString &label)
{
	if (slotIsCamera(slot))
		cfg_.cameras[slot].label = label.toStdString();
	else if (slot >= kCameraCount && slot < kSlotCount)
		cfg_.playback(slot).label = label.toStdString();
	emit stateChanged();
}

// ---------------------------------------------------------------------------
// operator actions

bool Controller::selectSlot(int slot)
{
	if (replayCueMode() && slotIsReplayCue(slot))
		return rcSelect(slot);
	std::string msg;
	bool ok = switch_.selectPreview(slot, &msg);
	if (!ok)
		setMessage(QString("%1: %2").arg(slotIsCamera(slot) ? QString("Camera %1").arg(slot + 1)
								    : slotLabel(slot),
						 QString::fromStdString(msg)),
			   true);
	emit stateChanged();
	return ok;
}

bool Controller::cut()
{
	if (replayCueMode()) {
		bool handled = false;
		bool ok = rcCut(&handled);
		if (handled) {
			emit stateChanged();
			return ok;
		}
	}
	std::string msg;
	bool ok = switch_.cut(&msg);
	if (!ok || !msg.empty())
		setMessage(QString::fromStdString(msg), !ok);
	emit stateChanged();
	return ok;
}

bool Controller::returnLive()
{
	if (replayCueMode()) {
		bool handled = false;
		bool ok = rcReturnLive(&handled);
		if (handled) {
			emit stateChanged();
			return ok;
		}
	}
	std::string msg;
	bool ok = switch_.returnLive(&msg);
	if (!ok)
		setMessage(QString("Return Live: %1").arg(QString::fromStdString(msg)), true);
	emit stateChanged();
	return ok;
}

bool Controller::setGraphicsVisible(bool visible)
{
	if (!graphics_.browserSupported()) {
		setMessage("Browser Source is not available in this OBS - camera graphics disabled", true);
		return false;
	}
	int n = graphics_.setVisible(visible);
	if (n == 0)
		setMessage("Camera graphics not set up - enter the graphics URL and run Create / Repair", true);
	emit stateChanged();
	return n > 0;
}

bool Controller::toggleGraphics()
{
	GraphicsVisibility v = graphics_.visibility();
	// From MIXED the operator applies one global state: ON.
	return setGraphicsVisible(v != GraphicsVisibility::On);
}

bool Controller::enableStudioMode()
{
	obs_frontend_set_preview_program_mode(true);
	emit stateChanged();
	return obs_frontend_preview_program_mode_active();
}

bool Controller::loadPlayback(int slot, const QString &path)
{
	std::string msg;
	bool ok = playback_.load(slot, path.toStdString(), &msg);
	if (!ok)
		setMessage(QString("%1: %2").arg(slotLabel(slot), QString::fromStdString(msg)), true);
	emit stateChanged();
	return ok;
}

bool Controller::loadAdsPlaylist(const QStringList &files)
{
	std::vector<std::string> list;
	for (const QString &f : files)
		list.push_back(f.toStdString());
	std::string msg;
	bool ok = playback_.loadPlaylist(kSlotAds, list, &msg);
	setMessage(QString("%1: %2").arg(slotLabel(kSlotAds), QString::fromStdString(msg)), !ok);
	emit stateChanged();
	return ok;
}

bool Controller::applyIntermissionUrl(const QString &url, QString *message)
{
	std::string msg;
	bool ok = graphics_.setPlaybackOverlay(kSlotIntermission, true, url.trimmed().toStdString(), &msg);
	if (message)
		*message = msg.empty() ? QString("Intermission page applied") : QString::fromStdString(msg);
	emit stateChanged();
	return ok;
}

bool Controller::armPlayback(int slot)
{
	std::string msg;
	bool ok = playback_.rearm(slot, &msg);
	if (!ok)
		setMessage(QString("%1: %2").arg(slotLabel(slot), QString::fromStdString(msg)), true);
	emit stateChanged();
	return ok;
}

bool Controller::playPausePlayback(int slot)
{
	std::string msg;
	bool ok = playback_.playPause(slot, &msg);
	if (!ok)
		setMessage(QString::fromStdString(msg), true);
	emit stateChanged();
	return ok;
}

bool Controller::restartPlayback(int slot)
{
	std::string msg;
	bool ok = playback_.restart(slot, &msg);
	if (!ok)
		setMessage(QString::fromStdString(msg), true);
	emit stateChanged();
	return ok;
}

// ---------------------------------------------------------------------------
// MatchFrame Replay Cue provider
//
// Replay Cue owns its replay capture, cue list, player, transitions and highlights reels.
// The multiview stages its scenes in Preview and asks Replay Cue to take them to air, so
// Replay Cue's own transitions, slow motion and automatic return still apply.

bool Controller::replayCueAction(const char *requestType, obs_data_t *data)
{
	std::string err;
	bool ok = rc_.action(requestType, data, &err);
	if (!ok)
		setMessage(QString::fromStdString(err), true);
	rc_.refresh(true);
	emit stateChanged();
	return ok;
}

bool Controller::replayCueSelectRelative(int delta)
{
	rc_.refresh(true);
	const auto &ids = rc_.eventIds();
	if (ids.empty()) {
		setMessage("Replay Cue has no replays yet", true);
		return false;
	}
	int index = (int)ids.size() - 1; // nothing selected: start from the latest
	for (size_t i = 0; i < ids.size(); i++)
		if (ids[i] == rc_.status().selectedEvent)
			index = (int)i;
	index = std::clamp(index + delta, 0, (int)ids.size() - 1);
	OBSDataAutoRelease d = obs_data_create();
	obs_data_set_string(d, "id", ids[(size_t)index].c_str());
	return replayCueAction("SelectEvent", d);
}

void Controller::setGraphicsProgramOnly(bool programOnly)
{
	cfg_.graphicsProgramOnly = programOnly;
	applyCleanFeed();
	emit stateChanged();
}

void Controller::applyCleanFeed()
{
	setCleanFeedEnabled(cfg_.graphicsProgramOnly);
	// The filter is a no-op when the option is off, so it stays on the sources either way.
	OBSSourceAutoRelease cam = bank_.cameraGraphics();
	ensureCleanFeedFilter(cam);
	// Playback overlays only: the Intermission page is that scene's content, so its tile
	// shows it.
	for (int slot : {kSlotReplay, kSlotHighlights, kSlotAds}) {
		OBSSourceAutoRelease pg = bank_.playbackGraphics(slot);
		ensureCleanFeedFilter(pg);
	}
}

void Controller::setPlaybackProvider(bool replayCue)
{
	cfg_.playbackProvider = replayCue ? "replaycue" : "builtin";
	rcHighlightsStaged_ = false;
	if (replayCue)
		rc_.refresh(true);
	obs_log(LOG_INFO, "Replay / Highlights provider: %s", replayCue ? "MatchFrame Replay Cue" : "built-in files");
	emit stateChanged();
}

void Controller::setReplayCueScenes(const std::string &replay, const std::string &highlights)
{
	cfg_.replayCueReplayScene = replay;
	cfg_.replayCueHighlightsScene = highlights;
	emit stateChanged();
}

bool Controller::rcSelect(int slot)
{
	if (!obs_frontend_preview_program_mode_active()) {
		setMessage(rejectMessage(Reject::StudioModeOff), true);
		return false;
	}
	rc_.refresh(true);
	if (!rc_.status().ok) {
		setMessage(QString::fromStdString(rc_.status().error), true);
		return false;
	}
	if (slot == kSlotReplay) {
		if (rc_.status().eventCount == 0) {
			setMessage("Replay Cue has no replays yet - press Instant Replay (or Mark IN / OUT)", true);
			return false;
		}
		// Replay Cue loads the selected (else latest) replay paused and puts its Replay
		// scene in Preview; CUT then asks it to play.
		rcHighlightsStaged_ = false;
		bool ok = replayCueAction("CueEvent");
		if (ok)
			rcCueRequestedMs_ = nowMs();
		return ok;
	}
	const std::string hl = replayCueScenes().second;
	OBSSourceAutoRelease scene = hl.empty() ? nullptr : obs_get_source_by_name(hl.c_str());
	if (!scene || !obs_scene_from_source(scene)) {
		setMessage("Replay Cue has not created its Replay scene yet - play one replay in Replay Cue first",
			   true);
		return false;
	}
	obs_frontend_set_current_preview_scene(scene);
	rcHighlightsStaged_ = true;
	setMessage(QString("Highlights staged (%1) - CUT plays the reel from Replay Cue")
			   .arg(QString::fromStdString(rc_.status().phaseName.empty() ? "all highlights"
										      : rc_.status().phaseName)),
		   false);
	emit stateChanged();
	return true;
}

bool Controller::rcCut(bool *handled)
{
	*handled = false;
	OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
	const std::string pv = nameOf(preview);
	const auto names = replayCueScenes();
	if (pv.empty() || (pv != names.first && pv != names.second))
		return false; // not a Replay Cue scene: normal CUT
	*handled = true;
	if (nowMs() < rcDebounceUntil_) {
		setMessage("Replay Cue take already requested", true);
		return false;
	}
	rc_.refresh(false);
	const ReplayCueStatus &st = rc_.status();
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	bool ok;
	if (rcHighlightsStaged_ && pv == names.second) {
		if (program.Get() == preview.Get() && st.highlights) {
			setMessage("The highlights reel is already on air", false);
			return true;
		}
		ok = replayCueAction("PlayHighlights");
		rcHighlightsStaged_ = false;
	} else {
		// Replay Cue handles requests in order on its UI thread and refreshes its status
		// snapshot on its own poll, so right after our CueEvent the status may not show it
		// yet: a Resume sent now is still processed after the cue.
		const bool cueJustRequested = nowMs() - rcCueRequestedMs_ < 5000;
		if (st.eventId.empty() && !st.cued && !cueJustRequested) {
			setMessage("Nothing is cued in Replay Cue - press 9 (or cue a replay in Replay Cue)", true);
			return false;
		}
		// Replay Cue takes its scene to air with its own replay transition and remembers
		// the live scene for its automatic / manual return.
		ok = replayCueAction("Resume");
	}
	if (ok)
		rcDebounceUntil_ = nowMs() + 1500;
	return ok;
}

bool Controller::rcReturnLive(bool *handled)
{
	*handled = false;
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	const std::string pg = nameOf(program);
	const auto names = replayCueScenes();
	if (pg.empty() || (pg != names.first && pg != names.second))
		return false;
	*handled = true;
	rcDebounceUntil_ = 0;
	bool ok = replayCueAction("ReturnToLive");
	// If Replay Cue cannot return (it did not take the scene live itself and has no live
	// scene configured), cut to the last live camera so the operator is never stuck.
	QTimer::singleShot(2500, this, [this, names]() {
		OBSSourceAutoRelease now = obs_frontend_get_current_scene();
		const std::string n = nameOf(now);
		if ((n == names.first || n == names.second) && !transitionInProgress()) {
			std::string msg;
			if (switch_.returnLive(&msg))
				setMessage("Replay Cue did not return to live - cut to the last live camera instead",
					   true);
			else
				setMessage(QString("Return Live: %1").arg(QString::fromStdString(msg)), true);
			emit stateChanged();
		}
	});
	return ok;
}

// ---------------------------------------------------------------------------
// setup actions

bool Controller::structuralAllowed(QString *why) const
{
	if (!collectionReady()) {
		if (why)
			*why = "Scene collection is loading";
		return false;
	}
	if (outputsStreaming() || outputsRecording()) {
		if (why)
			*why = "Structural setup is disabled while streaming or recording "
			       "(switching, playback loading and graphics visibility still work)";
		return false;
	}
	return true;
}

RepairReport Controller::createOrRepair()
{
	RepairReport r;
	QString why;
	if (!structuralAllowed(&why)) {
		r.warn(why.toStdString());
		setMessage(why, true);
		return r;
	}
	r = bank_.createOrRepair();
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	// Newly created media sources open their file immediately; (re)arm both slots.
	playback_.resume(bank_.slotOfScene(program));
	switch_.reconcile();
	applyCleanFeed();
	setMessage(QString("Create / Repair: %1 created, %2 reused, %3 warning(s)")
			   .arg(r.created)
			   .arg(r.reused)
			   .arg(r.warnings),
		   r.warnings > 0);
	emit stateChanged();
	return r;
}

bool Controller::assignInput(int cam, const std::string &uuid, QString *message)
{
	QString why;
	if (!structuralAllowed(&why)) {
		if (message)
			*message = why;
		return false;
	}
	std::string msg;
	bool ok = bank_.assignInput(cam, uuid, &msg);
	if (message)
		*message = QString::fromStdString(msg);
	emit stateChanged();
	return ok;
}

bool Controller::applyFraming(int cam, Framing framing, QString *message)
{
	std::string msg;
	bool ok = bank_.applyFraming(cam, framing, &msg);
	if (message)
		*message = QString::fromStdString(msg);
	emit stateChanged();
	return ok;
}

bool Controller::applyGraphicsUrl(const QString &url, QString *message)
{
	std::string msg;
	bool ok = graphics_.applyUrl(url.trimmed().toStdString(), &msg);
	if (message)
		*message = QString::fromStdString(msg);
	emit stateChanged();
	return ok;
}

bool Controller::setPlaybackOverlay(int slot, bool enabled, const QString &url, QString *message)
{
	std::string msg;
	bool ok = graphics_.setPlaybackOverlay(slot, enabled, url.trimmed().toStdString(), &msg);
	if (message)
		*message = QString::fromStdString(msg);
	emit stateChanged();
	applyCleanFeed();
	return ok;
}

// ---------------------------------------------------------------------------
// window

void Controller::showMultiview()
{
	if (shutdown_)
		return;
	if (!window_) {
		auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		window_ = new MultiviewWindow(this, main);
	}
	window_->showAndRaise();
}

// ---------------------------------------------------------------------------
// OBS integration

void Controller::onTimer()
{
	if (!collectionReady())
		return;
	switch_.tick();
	switch_.reconcile();
	if (replayCueMode() && (rcTick_++ % 3) == 0)
		rc_.refresh(rcTick_ % 21 == 1); // status ~3x/s, replay list every ~2 s
	emit stateChanged();
}

void Controller::suspend()
{
	if (suspended_)
		return;
	rcHighlightsStaged_ = false;
	suspended_ = true;
	switch_.setSuspended(true);
	playback_.suspend();
	if (window_)
		window_->releaseRendering();
	obs_log(LOG_INFO, "switching suspended (scene collection change)");
	emit collectionChanged();
	emit stateChanged();
}

void Controller::resume()
{
	suspended_ = false;
	switch_.setSuspended(false);
	bank_.refreshNames();
	switch_.restoreLastCamera();
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	playback_.resume(bank_.slotOfScene(program));
	switch_.reconcile();
	applyCleanFeed();
	obs_log(LOG_INFO, "scene bank %s: %s", cfg_.bankId.c_str(),
		bank_.anySceneExists() ? "resolved" : "not created yet (use Setup → Create / Repair)");
	if (cfg_.migratedFromV1)
		obs_log(LOG_INFO, "configuration migrated from schema v1 (names) to v%d (UUIDs)", kConfigVersion);
	emit collectionChanged();
	emit stateChanged();
}

void Controller::onFrontendEvent(enum obs_frontend_event event)
{
	if (shutdown_)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		rc_.start(this, [this]() {
			if (replayCueMode() && collectionReady()) {
				rc_.refresh(true);
				emit stateChanged();
			}
		});
		loaded_ = true;
		resume();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		awaitingLoad_ = true;
		suspend();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		suspend();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (awaitingLoad_) {
			// OBS creates a new (or duplicated) collection without calling the save
			// callbacks' load: never carry the previous collection's bank into it.
			cfg_ = makeDefaultConfig(randomId());
			awaitingLoad_ = false;
			obs_log(LOG_INFO, "new scene collection: starting an empty MatchFrame configuration");
		}
		if (loaded_)
			resume();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
		switch_.onTransitionFinished();
		rcDebounceUntil_ = 0; // a Replay Cue take has landed: the next CUT is a new request
		[[fallthrough]];
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		if (collectionReady()) {
			switch_.reconcile();
			emit stateChanged();
		}
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		shutdown();
		break;
	default:
		break;
	}
}

void Controller::onSave(obs_data_t *data, bool saving)
{
	if (saving) {
		if (!suspended_)
			bank_.refreshNames();
		OBSDataAutoRelease obj = obs_data_create();
		saveConfig(cfg_, obj);
		obs_data_set_obj(data, kSaveKey, obj);
	} else {
		OBSDataAutoRelease obj = obs_data_get_obj(data, kSaveKey);
		cfg_ = loadConfig(obj, randomId());
		awaitingLoad_ = false;
		obs_log(LOG_INFO, "loaded MatchFrame configuration (bank %s, schema v%d)", cfg_.bankId.c_str(),
			cfg_.migratedFromV1 ? 1 : kConfigVersion);
	}
}

void Controller::shutdown()
{
	if (shutdown_)
		return;
	shutdown_ = true;
	timer_.stop();
	rc_.stop();
	saveHotkeys();
	if (window_) {
		window_->saveGeometryPrefs();
		delete window_.data(); // destroys the native display while OBS graphics still run
	}
	playback_.suspend();
	switch_.setSuspended(true);
	suspended_ = true;
}

// ---------------------------------------------------------------------------
// hotkeys (frontend actions, unbound by default; bindings stored in the module config)

namespace {
constexpr int kActionCut = 100;
constexpr int kActionReturnLive = 101;

struct HotkeyDef {
	const char *name;
	const char *description;
	int action; // bank slot to preview, or kActionCut / kActionReturnLive
};
const HotkeyDef kHotkeys[kHotkeyCount] = {
	{"MatchFrame.PreviewCam1", "MatchFrame: Preview Camera 1", 0},
	{"MatchFrame.PreviewCam2", "MatchFrame: Preview Camera 2", 1},
	{"MatchFrame.PreviewCam3", "MatchFrame: Preview Camera 3", 2},
	{"MatchFrame.PreviewCam4", "MatchFrame: Preview Camera 4", 3},
	{"MatchFrame.PreviewCam5", "MatchFrame: Preview Camera 5", 4},
	{"MatchFrame.PreviewCam6", "MatchFrame: Preview Camera 6", 5},
	{"MatchFrame.PreviewCam7", "MatchFrame: Preview Camera 7", 6},
	{"MatchFrame.PreviewCam8", "MatchFrame: Preview Camera 8", 7},
	{"MatchFrame.PreviewReplay", "MatchFrame: Preview Replay", kSlotReplay},
	{"MatchFrame.PreviewHighlights", "MatchFrame: Preview Highlights", kSlotHighlights},
	{"MatchFrame.Cut", "MatchFrame: CUT (Preview → Program)", kActionCut},
	{"MatchFrame.ReturnLive", "MatchFrame: Return Live (cut to last camera)", kActionReturnLive},
	{"MatchFrame.PreviewIntermission", "MatchFrame: Preview Intermission", kSlotIntermission},
	{"MatchFrame.PreviewAds", "MatchFrame: Preview Video Ads", kSlotAds},
};

void hotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const int action = (int)(intptr_t)data;
	Controller *c = Controller::instance();
	if (!c)
		return;
	// Hotkeys fire on OBS's hotkey thread: perform the action on the UI thread.
	QMetaObject::invokeMethod(
		c,
		[action]() {
			Controller *ctl = Controller::instance();
			if (!ctl || !ctl->collectionReady())
				return;
			if (action == kActionCut)
				ctl->cut();
			else if (action == kActionReturnLive)
				ctl->returnLive();
			else
				ctl->selectSlot(action);
		},
		Qt::QueuedConnection);
}

std::string hotkeyFile()
{
	char *p = obs_module_config_path("hotkeys.json");
	std::string s = p ? p : "";
	bfree(p);
	return s;
}
} // namespace

void Controller::registerHotkeys()
{
	for (int i = 0; i < kHotkeyCount; i++) {
		if (hotkeys_[i] != OBS_INVALID_HOTKEY_ID)
			continue;
		hotkeys_[i] = obs_hotkey_register_frontend(kHotkeys[i].name, kHotkeys[i].description, hotkeyCallback,
							   (void *)(intptr_t)kHotkeys[i].action);
	}
	loadHotkeys();
}

void Controller::unregisterHotkeys()
{
	for (auto &id : hotkeys_) {
		if (id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(id);
		id = OBS_INVALID_HOTKEY_ID;
	}
}

void Controller::loadHotkeys()
{
	std::string path = hotkeyFile();
	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		return;
	for (int i = 0; i < kHotkeyCount; i++) {
		OBSDataArrayAutoRelease arr = obs_data_get_array(data, kHotkeys[i].name);
		if (arr && hotkeys_[i] != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_load(hotkeys_[i], arr);
	}
}

void Controller::saveHotkeys()
{
	std::string path = hotkeyFile();
	if (path.empty())
		return;
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	OBSDataAutoRelease data = obs_data_create();
	for (int i = 0; i < kHotkeyCount; i++) {
		if (hotkeys_[i] == OBS_INVALID_HOTKEY_ID)
			continue;
		OBSDataArrayAutoRelease arr = obs_hotkey_save(hotkeys_[i]);
		obs_data_set_array(data, kHotkeys[i].name, arr);
	}
	obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
}

} // namespace mf
