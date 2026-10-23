#include "replay-cue-link.hpp"

#include <plugin-support.h>

#include <QMetaObject>

#include <cassert>
#include <cstring>

#include "third-party/obs-websocket-api.h"

namespace mf {

// obs-websocket RequestStatus::Success
static constexpr unsigned int kRequestSuccess = 100;

ReplayCueLink::~ReplayCueLink()
{
	stop();
}

void ReplayCueLink::start(QObject *context, std::function<void()> wake)
{
	context_ = context;
	wake_ = std::move(wake);
	if (!registered_)
		registered_ = obs_websocket_register_event_callback(&ReplayCueLink::onWebsocketEvent, this);
	if (!registered_)
		obs_log(LOG_INFO, "Replay Cue link: obs-websocket not available (Replay Cue control disabled)");
}

void ReplayCueLink::stop()
{
	if (registered_)
		obs_websocket_unregister_event_callback(&ReplayCueLink::onWebsocketEvent, this);
	registered_ = false;
}

void ReplayCueLink::onWebsocketEvent(uint64_t, const char *typeName, const char *data, void *param)
{
	// Called on an obs-websocket thread: only look at Replay Cue's vendor events, then
	// hop to the UI thread. Queued calls are dropped if the context is gone.
	if (!typeName || strcmp(typeName, "VendorEvent") != 0 || !data || !strstr(data, kReplayCueVendor))
		return;
	auto *self = static_cast<ReplayCueLink *>(param);
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

OBSDataAutoRelease ReplayCueLink::call(const char *requestType, obs_data_t *requestData, std::string *error)
{
	OBSDataAutoRelease req = obs_data_create();
	obs_data_set_string(req, "vendorName", kReplayCueVendor);
	obs_data_set_string(req, "requestType", requestType);
	if (requestData)
		obs_data_set_obj(req, "requestData", requestData);

	obs_websocket_request_response *resp = obs_websocket_call_request("CallVendorRequest", req);
	if (!resp) {
		if (error)
			*error = "obs-websocket is not available";
		return nullptr;
	}
	OBSDataAutoRelease out;
	if (resp->status_code != kRequestSuccess) {
		if (error)
			*error = resp->comment && *resp->comment
					 ? std::string("Replay Cue: ") + resp->comment
					 : "Replay Cue request failed (code " + std::to_string(resp->status_code) + ")";
	} else if (resp->response_data) {
		OBSDataAutoRelease full = obs_data_create_from_json(resp->response_data);
		out = obs_data_get_obj(full, "responseData");
		if (!out)
			out = obs_data_create();
	} else {
		out = obs_data_create();
	}
	obs_websocket_request_response_free(resp);
	return out;
}

bool ReplayCueLink::action(const char *requestType, obs_data_t *requestData, std::string *error)
{
	OBSDataAutoRelease r = call(requestType, requestData, error);
	if (!r)
		return false;
	if (obs_data_has_user_value(r, "ok") && !obs_data_get_bool(r, "ok")) {
		if (error) {
			const char *e = obs_data_get_string(r, "error");
			*error = std::string("Replay Cue: ") + (e && *e ? e : "request refused");
		}
		return false;
	}
	obs_log(LOG_INFO, "Replay Cue: %s", requestType);
	return true;
}

void ReplayCueLink::refresh(bool events)
{
	std::string err;
	OBSDataAutoRelease st = call("GetStatus", nullptr, &err);
	if (st) {
		int keepCount = status_.eventCount;
		status_ = parseReplayCueStatus(st);
		status_.eventCount = keepCount;
	} else {
		status_ = ReplayCueStatus();
		status_.error = err.empty() ? "Replay Cue not detected" : err;
		eventIds_.clear();
		return;
	}
	if (!events)
		return;
	OBSDataAutoRelease ev = call("GetEvents", nullptr, nullptr);
	status_.eventCount = parseReplayCueEventCount(ev);
	eventIds_.clear();
	if (ev) {
		OBSDataArrayAutoRelease arr = obs_data_get_array(ev, "events");
		for (size_t i = 0, n = arr ? obs_data_array_count(arr) : 0; i < n; i++) {
			OBSDataAutoRelease item = obs_data_array_item(arr, i);
			eventIds_.push_back(obs_data_get_string(item, "id"));
		}
	}
}

std::vector<SceneCandidate> ReplayCueLink::sceneCandidates()
{
	std::vector<SceneCandidate> out;
	obs_enum_scenes(
		[](void *param, obs_source_t *scene) {
			auto *v = static_cast<std::vector<SceneCandidate> *>(param);
			SceneCandidate c;
			c.name = obs_source_get_name(scene);
			obs_scene_enum_items(
				obs_scene_from_source(scene),
				[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
					const char *id = obs_source_get_unversioned_id(obs_sceneitem_get_source(item));
					if (id && strcmp(id, kReplayCuePlayerId) == 0) {
						*static_cast<bool *>(p) = true;
						return false;
					}
					return true;
				},
				&c.hasPlayer);
			v->push_back(std::move(c));
			return true;
		},
		&out);
	return out;
}

std::pair<std::string, std::string> ReplayCueLink::scenes(const BankConfig &cfg)
{
	return pickReplayCueScenes(sceneCandidates(), cfg.replayCueReplayScene, cfg.replayCueHighlightsScene);
}

} // namespace mf
