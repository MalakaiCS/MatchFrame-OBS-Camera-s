// MatchFrame Multiview - plugin controller: owns the bank, controllers and the window.
//
// Everything here runs on the Qt UI thread (frontend events and save callbacks are
// delivered there; hotkeys and media signals are marshalled onto it).
#pragma once

#include "core/config.hpp"
#include "graphics-controller.hpp"
#include "multiview-window.hpp"
#include "playback-controller.hpp"
#include "replay-cue-link.hpp"
#include "scene-bank.hpp"
#include "switch-controller.hpp"

#include <obs-frontend-api.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <array>

namespace mf {

class Controller : public QObject {
	Q_OBJECT

public:
	Controller();
	~Controller() override;

	static Controller *instance();

	BankConfig &config() { return cfg_; }
	SceneBank &bank() { return bank_; }
	GraphicsController &graphics() { return graphics_; }
	PlaybackController &playback() { return playback_; }
	SwitchController &switcher() { return switch_; }

	// --- operator actions (UI thread) ---
	bool selectSlot(int slot);
	bool cut();
	bool returnLive();
	bool setGraphicsVisible(bool visible);
	bool toggleGraphics();
	bool enableStudioMode();
	bool loadPlayback(int slot, const QString &path);
	bool armPlayback(int slot);
	bool playPausePlayback(int slot);
	bool restartPlayback(int slot);
	void setSlotLabel(int slot, const QString &label);

	// --- setup actions ---
	bool structuralAllowed(QString *why) const;
	RepairReport createOrRepair();
	bool assignInput(int cam, const std::string &uuid, QString *message);
	bool applyFraming(int cam, Framing framing, QString *message);
	bool applyGraphicsUrl(const QString &url, QString *message);
	void setGraphicsProgramOnly(bool programOnly);
	bool setPlaybackOverlay(int slot, bool enabled, const QString &url, QString *message);

	// --- MatchFrame Replay Cue provider ---
	bool replayCueMode() const { return cfg_.playbackProvider == "replaycue"; }
	ReplayCueLink &replayCue() { return rc_; }
	std::pair<std::string, std::string> replayCueScenes() const { return ReplayCueLink::scenes(cfg_); }
	bool replayCueHighlightsStaged() const { return rcHighlightsStaged_; }
	// Runs a ReplayCue vendor request from a multiview control and reports failures.
	bool replayCueAction(const char *requestType, obs_data_t *data = nullptr);
	bool replayCueSelectRelative(int delta);
	void setPlaybackProvider(bool replayCue);
	void setReplayCueScenes(const std::string &replay, const std::string &highlights);

	QString slotLabel(int slot) const;
	bool collectionReady() const { return loaded_ && !suspended_; }
	bool outputsStreaming() const;
	bool outputsRecording() const;

	QString lastMessage() const { return message_; }
	bool lastMessageIsError() const { return messageError_; }
	void setMessage(const QString &msg, bool error);

	void showMultiview();
	MultiviewWindow *window() const { return window_; }

	// --- OBS integration ---
	void onFrontendEvent(enum obs_frontend_event event);
	void onSave(obs_data_t *data, bool saving);
	void registerHotkeys();
	void unregisterHotkeys();
	void shutdown();

signals:
	void stateChanged();
	void messageChanged(const QString &message, bool error);
	void collectionChanged();

private slots:
	void onTimer();

private:
	void suspend();
	void resume();
	void applyCleanFeed();
	void saveHotkeys();
	void loadHotkeys();

	bool rcSelect(int slot);
	bool rcCut(bool *handled);
	bool rcReturnLive(bool *handled);

	BankConfig cfg_;
	SceneBank bank_{cfg_};
	GraphicsController graphics_{bank_};
	PlaybackController playback_{bank_};
	SwitchController switch_{bank_, playback_};

	QTimer timer_;
	QPointer<MultiviewWindow> window_;
	QString message_;
	bool messageError_ = false;
	bool loaded_ = false;
	bool suspended_ = true;
	bool shutdown_ = false;
	bool awaitingLoad_ = false;
	ReplayCueLink rc_;
	bool rcHighlightsStaged_ = false;
	int64_t rcDebounceUntil_ = 0;
	int64_t rcCueRequestedMs_ = -100000;
	int rcTick_ = 0;
	std::array<obs_hotkey_id, 12> hotkeys_;
};

} // namespace mf
