// MatchFrame Multiview - link to the MatchFrame Replay Cue plugin.
//
// Replay Cue registers the obs-websocket vendor "ReplayCue". Both plugins run in the
// same OBS process, so requests are made in-process through obs-websocket's plugin API
// (CallVendorRequest) - no network connection, no password, no link-time dependency.
// If Replay Cue (or obs-websocket) is not installed every call fails cleanly.
#pragma once

#include "core/config.hpp"
#include "core/replay-cue-status.hpp"

#include <obs.hpp>

#include <QObject>
#include <QPointer>

#include <functional>
#include <string>
#include <vector>

namespace mf {

class ReplayCueLink {
public:
	ReplayCueLink() = default;
	~ReplayCueLink();
	ReplayCueLink(const ReplayCueLink &) = delete;
	ReplayCueLink &operator=(const ReplayCueLink &) = delete;

	// Subscribes to Replay Cue's vendor events; `wake` runs on the UI thread (via context).
	void start(QObject *context, std::function<void()> wake);
	void stop();

	// Calls a ReplayCue vendor request; returns its responseData (nullptr + error on failure).
	OBSDataAutoRelease call(const char *requestType, obs_data_t *requestData, std::string *error);
	// Calls an action request and checks Replay Cue's "ok" flag.
	bool action(const char *requestType, obs_data_t *requestData, std::string *error);

	// Re-reads GetStatus (cheap; Replay Cue serves a snapshot) and, when `events` is set, GetEvents.
	void refresh(bool events);
	const ReplayCueStatus &status() const { return status_; }
	const std::vector<std::string> &eventIds() const { return eventIds_; }

	// Scenes in the current collection, flagging those that hold a Replay Cue Player.
	static std::vector<SceneCandidate> sceneCandidates();
	// {replay scene, highlights scene} names for the tiles (empty = not created yet).
	static std::pair<std::string, std::string> scenes(const BankConfig &cfg);

private:
	static void onWebsocketEvent(uint64_t type, const char *typeName, const char *data, void *param);

	ReplayCueStatus status_;
	std::vector<std::string> eventIds_;
	bool registered_ = false;
	QPointer<QObject> context_;
	std::function<void()> wake_;
};

} // namespace mf
