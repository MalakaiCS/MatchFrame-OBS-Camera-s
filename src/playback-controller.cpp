#include "playback-controller.hpp"

#include <plugin-support.h>
#include <util/platform.h>

#include <QFileInfo>
#include <QMetaObject>

namespace mf {

MediaState toMediaState(enum obs_media_state s)
{
	switch (s) {
	case OBS_MEDIA_STATE_PLAYING:
		return MediaState::Playing;
	case OBS_MEDIA_STATE_OPENING:
		return MediaState::Opening;
	case OBS_MEDIA_STATE_BUFFERING:
		return MediaState::Buffering;
	case OBS_MEDIA_STATE_PAUSED:
		return MediaState::Paused;
	case OBS_MEDIA_STATE_STOPPED:
		return MediaState::Stopped;
	case OBS_MEDIA_STATE_ENDED:
		return MediaState::Ended;
	case OBS_MEDIA_STATE_ERROR:
		return MediaState::Error;
	default:
		return MediaState::None;
	}
}

static int idx(int slot)
{
	return slot == kSlotReplay ? 0 : 1;
}

static bool fileExists(const std::string &p)
{
	return !p.empty() && QFileInfo(QString::fromStdString(p)).isFile();
}

PlaybackController::PlaybackController(SceneBank &bank) : bank_(bank) {}

PlaybackController::~PlaybackController()
{
	disconnectSignals(kSlotReplay);
	disconnectSignals(kSlotHighlights);
}

PlaybackView PlaybackController::view(int slot)
{
	PlaybackView v;
	const PlaybackSlotLogic &l = logic(slot);
	v.state = l.state();
	v.file = l.file();
	v.fileName = QFileInfo(QString::fromStdString(l.file())).fileName().toStdString();
	v.error = l.error();
	v.onProgram = l.onProgram();
	v.activeElsewhere = l.activeElsewhere();
	OBSSourceAutoRelease media = bank_.mediaSource(slot);
	v.sourceExists = media != nullptr;
	if (media) {
		v.durationMs = obs_source_media_get_duration(media);
		v.timeMs = obs_source_media_get_time(media);
		// OBS reports time 0 once a clip has ended; show it held at the end instead.
		if (v.durationMs > 0 && (v.timeMs > v.durationMs || l.state() == PlaybackState::Ended))
			v.timeMs = v.durationMs;
		v.modifyBlockReason = PlaybackSlotLogic::modifyBlockReason(l.onProgram(), obs_source_active(media));
		v.canModify = v.modifyBlockReason.empty();
	} else {
		v.modifyBlockReason = "Playback scene not set up - run Create / Repair";
	}
	return v;
}

bool PlaybackController::readyForTake(int slot) const
{
	return logic(slot).state() == PlaybackState::Ready;
}

void PlaybackController::reloadFile(obs_source_t *media, const std::string &path)
{
	OBSDataAutoRelease cur = obs_source_get_settings(media);
	const char *curPath = obs_data_get_string(cur, "local_file");
	OBSDataAutoRelease s = obs_data_create();
	SceneBank::applyMediaDefaults(s);
	if (curPath && path == curPath) {
		// Same path: force the media source to reopen (file may have been re-exported,
		// or playback ended and the decoder is no longer active).
		obs_data_set_string(s, "local_file", "");
		obs_source_update(media, s);
	}
	obs_data_set_string(s, "local_file", path.c_str());
	obs_source_update(media, s);
	// With restart_on_activate off, OBS starts decoding immediately; hold it at once.
	// The source is not active (not on Program), so nothing reaches the output mix.
	obs_source_media_play_pause(media, true);
}

void PlaybackController::perform(int slot, PlaybackAction action, obs_source_t *media)
{
	if (!media || action == PlaybackAction::None)
		return;
	PlaybackSlotLogic &l = logic(slot);
	switch (action) {
	case PlaybackAction::SetFileAndOpen:
		reloadFile(media, l.file());
		obs_log(LOG_INFO, "%s: loading '%s'", slot == kSlotReplay ? "replay" : "highlights",
			QFileInfo(QString::fromStdString(l.file())).fileName().toUtf8().constData());
		break;
	case PlaybackAction::PauseSeekStart: {
		MediaState st = toMediaState(obs_source_media_get_state(media));
		if (st == MediaState::Ended || st == MediaState::Stopped || st == MediaState::None ||
		    st == MediaState::Error)
			reloadFile(media, l.file()); // seeking needs an active decoder
		obs_source_media_play_pause(media, true);
		obs_source_media_set_time(media, 0);
		break;
	}
	case PlaybackAction::Restart:
		obs_source_media_restart(media);
		obs_log(LOG_INFO, "%s: on Program - playing from the beginning",
			slot == kSlotReplay ? "replay" : "highlights");
		break;
	case PlaybackAction::PlayPause: {
		bool playing = obs_source_media_get_state(media) == OBS_MEDIA_STATE_PLAYING;
		obs_source_media_play_pause(media, playing);
		break;
	}
	default:
		break;
	}
}

bool PlaybackController::load(int slot, const std::string &path, std::string *message)
{
	OBSSourceAutoRelease media = bank_.mediaSource(slot);
	if (!media) {
		if (message)
			*message = "Playback scene not set up - run Create / Repair first";
		return false;
	}
	PlaybackSlotLogic &l = logic(slot);
	std::string why;
	PlaybackAction a =
		l.requestLoad(path, fileExists(path), l.onProgram(), obs_source_active(media), nowMs(), &why);
	if (a == PlaybackAction::None) {
		if (message)
			*message = why;
		return false;
	}
	bank_.config().playback(slot).filePath = path;
	perform(slot, a, media);
	connectSignals(slot, media);
	if (message)
		*message = "Loading…";
	return true;
}

bool PlaybackController::rearm(int slot, std::string *message)
{
	OBSSourceAutoRelease media = bank_.mediaSource(slot);
	if (!media) {
		if (message)
			*message = "Playback scene not set up";
		return false;
	}
	PlaybackSlotLogic &l = logic(slot);
	if (!l.hasFile() && !bank_.config().playback(slot).filePath.empty())
		return load(slot, bank_.config().playback(slot).filePath, message);
	std::string why;
	PlaybackAction a = l.requestRearm(l.onProgram(), obs_source_active(media), nowMs(), &why);
	if (a == PlaybackAction::None) {
		if (message)
			*message = why;
		return false;
	}
	perform(slot, a, media);
	return true;
}

bool PlaybackController::playPause(int slot, std::string *message)
{
	OBSSourceAutoRelease media = bank_.mediaSource(slot);
	PlaybackAction a = logic(slot).transport(PlaybackAction::PlayPause);
	if (!media || a == PlaybackAction::None) {
		if (message)
			*message = "Nothing loaded";
		return false;
	}
	perform(slot, a, media);
	return true;
}

bool PlaybackController::restart(int slot, std::string *message)
{
	OBSSourceAutoRelease media = bank_.mediaSource(slot);
	PlaybackAction a = logic(slot).transport(PlaybackAction::Restart);
	if (!media || a == PlaybackAction::None) {
		if (message)
			*message = "Nothing loaded";
		return false;
	}
	if (!obs_source_showing(media)) {
		// OBS ignores restart for a source nobody is showing; reopen instead.
		reloadFile(media, logic(slot).file());
		obs_source_media_play_pause(media, false);
		return true;
	}
	perform(slot, a, media);
	return true;
}

void PlaybackController::poll(int programSlot)
{
	for (int slot : {kSlotReplay, kSlotHighlights}) {
		PlaybackSlotLogic &l = logic(slot);
		OBSSourceAutoRelease media = bank_.mediaSource(slot);
		if (!media) {
			if (l.state() != PlaybackState::Empty && l.state() != PlaybackState::Error)
				l.markError("Media source missing - run Create / Repair");
			disconnectSignals(slot);
			continue;
		}
		connectSignals(slot, media);
		const bool onProgram = programSlot == slot;
		const bool active = obs_source_active(media);
		const int64_t now = nowMs();

		// Program entry is acted on only once OBS reports the source active in the output.
		if (onProgram && !l.onProgram() && active)
			perform(slot, l.onProgramEnter(now), media);
		else if (!onProgram && l.onProgram())
			perform(slot, l.onProgramExit(active, now), media);
		else if (!onProgram && l.activeElsewhere() && !active)
			perform(slot, l.onBecameInactive(now), media);

		MediaObservation obs;
		obs.state = toMediaState(obs_source_media_get_state(media));
		obs.durationMs = obs_source_media_get_duration(media);
		obs.timeMs = obs_source_media_get_time(media);
		obs.hasFrame = obs_source_get_width(media) > 0;
		PlaybackState before = l.state();
		perform(slot, l.observe(obs, now), media);
		if (before != l.state())
			obs_log(LOG_INFO, "%s: %s -> %s%s%s", slot == kSlotReplay ? "replay" : "highlights",
				playbackStateName(before), playbackStateName(l.state()), l.error().empty() ? "" : " - ",
				l.error().c_str());
	}
}

void PlaybackController::resume(int programSlot)
{
	for (int slot : {kSlotReplay, kSlotHighlights}) {
		PlaybackSlotLogic &l = logic(slot);
		l.reset();
		OBSSourceAutoRelease media = bank_.mediaSource(slot);
		const std::string &path = bank_.config().playback(slot).filePath;
		if (!media || path.empty())
			continue;
		connectSignals(slot, media);
		bool onProgram = programSlot == slot;
		l.adoptExisting(path, fileExists(path), onProgram, nowMs());
		if (!onProgram && fileExists(path)) {
			// Saved sources start decoding on load (restart_on_activate is off): pause now.
			obs_source_media_play_pause(media, true);
		}
	}
}

void PlaybackController::suspend()
{
	for (int slot : {kSlotReplay, kSlotHighlights}) {
		disconnectSignals(slot);
		logic(slot).reset();
	}
}

void PlaybackController::onMediaSignal(void *data, calldata_t *)
{
	auto *self = static_cast<PlaybackController *>(data);
	// Signals arrive on OBS threads: hop to the UI thread before touching anything.
	QObject *ctx = self->context_.data();
	if (!ctx)
		return;
	std::function<void()> wake = self->wake_;
	QMetaObject::invokeMethod(
		ctx,
		[wake]() {
			if (wake)
				wake();
		},
		Qt::QueuedConnection);
}

static const char *kSignals[] = {"media_ended", "media_started", "media_pause", "media_play", "media_stopped"};

void PlaybackController::connectSignals(int slot, obs_source_t *media)
{
	OBSWeakSourceAutoRelease &w = connected_[idx(slot)];
	if (w && obs_weak_source_references_source(w, media))
		return;
	disconnectSignals(slot);
	signal_handler_t *sh = obs_source_get_signal_handler(media);
	for (const char *sig : kSignals)
		signal_handler_connect(sh, sig, onMediaSignal, this);
	w = obs_source_get_weak_source(media);
}

void PlaybackController::disconnectSignals(int slot)
{
	OBSWeakSourceAutoRelease &w = connected_[idx(slot)];
	if (!w)
		return;
	OBSSourceAutoRelease src = obs_weak_source_get_source(w);
	if (src) {
		signal_handler_t *sh = obs_source_get_signal_handler(src);
		for (const char *sig : kSignals)
			signal_handler_disconnect(sh, sig, onMediaSignal, this);
	}
	w = nullptr;
}

} // namespace mf
