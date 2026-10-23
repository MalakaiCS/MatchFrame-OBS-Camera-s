// MatchFrame Multiview - self-test only: a stand-in "ReplayCue" obs-websocket vendor.
//
// The real MatchFrame Replay Cue needs OBS 32.2+. To test the multiview's side of the
// integration in older test environments, this registers a vendor with the same name and
// implements the requests the multiview uses, following Replay Cue's documented behaviour
// (src/websocket-api.cpp, src/replay-controller.cpp in MalakaiCS/MatchFrame-OBS-Replay-Cue):
//   CueEvent      loads the selected replay paused and puts the Replay scene in Preview
//   Resume        takes the Replay scene live, remembering the live scene
//   PlayHighlights plays the reel live (same scene by default)
//   ReturnToLive  returns to the remembered live scene
// Built only with MF_ENABLE_SELFTEST; registered only when MATCHFRAME_SELFTEST_FAKE_REPLAYCUE=1.

#include "selftest.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QTimer>

#include <cassert>
#include <cstring>
#include <map>
#include <string>

#include "third-party/obs-websocket-api.h"

namespace mf::fakerc {

namespace {

struct State {
	obs_websocket_vendor vendor = nullptr;
	std::map<std::string, int> calls;
	bool returnWorks = true;
	bool cued = false;
	bool highlights = false;
	std::string state = "idle";
	std::string eventName;
	std::string selected = "e2";
	double speed = 1.0;
	OBSWeakSourceAutoRelease previous;
};

State &st()
{
	static State s;
	return s;
}

void later(std::function<void()> fn)
{
	// Replay Cue queues actions onto the UI thread; do the same.
	QTimer::singleShot(0, QCoreApplication::instance(), std::move(fn));
}

OBSSourceAutoRelease ensureReplayScene()
{
	OBSSourceAutoRelease scene = obs_get_source_by_name("Replay");
	if (scene)
		return scene;
	obs_scene_t *created = obs_scene_create("Replay");
	OBSDataAutoRelease s = obs_data_create();
	obs_data_set_int(s, "color", 0xFF4040C0);
	OBSSourceAutoRelease fill = obs_source_create("color_source_v3", "Fake Replay Cue Player", s, nullptr);
	obs_scene_add(created, fill);
	OBSSourceAutoRelease ret = obs_source_get_ref(obs_scene_get_source(created));
	obs_scene_release(created);
	return ret;
}

void goLive()
{
	OBSSourceAutoRelease replay = ensureReplayScene();
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	if (current.Get() != replay.Get()) {
		st().previous = obs_source_get_weak_source(current);
		obs_frontend_set_current_scene(replay);
	}
	if (st().vendor) {
		OBSDataAutoRelease d = obs_data_create();
		obs_data_set_string(d, "name", st().eventName.c_str());
		obs_websocket_vendor_emit_event(st().vendor, "PlaybackStarted", d);
	}
}

void ok(obs_data_t *response)
{
	obs_data_set_bool(response, "ok", true);
}

#define COUNT(name) st().calls[name]++

void reqGetStatus(obs_data_t *, obs_data_t *r, void *)
{
	COUNT("GetStatus");
	ok(r);
	obs_data_set_bool(r, "armed", true);
	obs_data_set_int(r, "pending_replays", 0);
	obs_data_set_string(r, "selected_event", st().selected.c_str());
	OBSDataAutoRelease pb = obs_data_create();
	obs_data_set_string(pb, "state", st().state.c_str());
	obs_data_set_string(pb, "event_id", st().eventName.empty() ? "" : st().selected.c_str());
	obs_data_set_string(pb, "event_name", st().eventName.c_str());
	obs_data_set_int(pb, "angle", 1);
	obs_data_set_string(pb, "angle_label", "Cam 1");
	obs_data_set_int(pb, "angle_count", 2);
	obs_data_set_double(pb, "speed", st().speed);
	obs_data_set_double(pb, "position", 0.0);
	obs_data_set_double(pb, "duration", 8.0);
	obs_data_set_bool(pb, "cued", st().cued);
	obs_data_set_bool(pb, "highlights", st().highlights);
	obs_data_set_obj(r, "playback", pb);
	OBSDataAutoRelease m = obs_data_create();
	obs_data_set_string(m, "phase_name", "Half Time");
	obs_data_set_bool(m, "in_break", true);
	obs_data_set_obj(r, "match", m);
}

void reqGetEvents(obs_data_t *, obs_data_t *r, void *)
{
	COUNT("GetEvents");
	ok(r);
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const char *id : {"e1", "e2"}) {
		OBSDataAutoRelease e = obs_data_create();
		obs_data_set_string(e, "id", id);
		obs_data_array_push_back(arr, e);
	}
	obs_data_set_array(r, "events", arr);
}

void reqCueEvent(obs_data_t *, obs_data_t *r, void *)
{
	COUNT("CueEvent");
	ok(r);
	later([]() {
		OBSSourceAutoRelease replay = ensureReplayScene();
		st().cued = true;
		st().highlights = false;
		st().state = "paused";
		st().eventName = st().selected == "e1" ? "Save" : "Goal 1";
		if (obs_frontend_preview_program_mode_active())
			obs_frontend_set_current_preview_scene(replay);
	});
}

void reqResume(obs_data_t *, obs_data_t *r, void *)
{
	COUNT("Resume");
	ok(r);
	later([]() {
		st().cued = false;
		st().state = "playing";
		goLive();
	});
}

void reqPlayHighlights(obs_data_t *, obs_data_t *r, void *)
{
	COUNT("PlayHighlights");
	ok(r);
	later([]() {
		st().cued = false;
		st().highlights = true;
		st().state = "playing";
		st().eventName = "Half Time highlights";
		goLive();
	});
}

void reqReturnToLive(obs_data_t *, obs_data_t *r, void *)
{
	COUNT("ReturnToLive");
	ok(r);
	later([]() {
		OBSSourceAutoRelease prev = obs_weak_source_get_source(st().previous);
		if (st().returnWorks && prev)
			obs_frontend_set_current_scene(prev);
		st().state = "idle";
		st().highlights = false;
		st().cued = false;
		st().eventName.clear();
	});
}

void reqSelectEvent(obs_data_t *req, obs_data_t *r, void *)
{
	COUNT("SelectEvent");
	st().selected = obs_data_get_string(req, "id");
	ok(r);
}

void reqSetSpeed(obs_data_t *req, obs_data_t *r, void *)
{
	COUNT("SetSpeed");
	if (obs_data_has_user_value(req, "percent"))
		st().speed = obs_data_get_double(req, "percent") / 100.0;
	ok(r);
}

#define SIMPLE(fn, name)                                     \
	void fn(obs_data_t *, obs_data_t *r, void *) \
	{                                                    \
		COUNT(name);                                 \
		ok(r);                                       \
	}
SIMPLE(reqInstant, "InstantReplay")
SIMPLE(reqMarkIn, "MarkIn")
SIMPLE(reqMarkOut, "MarkOut")
SIMPLE(reqPlayPause, "PlayPause")
SIMPLE(reqNextAngle, "NextAngle")
SIMPLE(reqPrevAngle, "PrevAngle")
#undef SIMPLE

} // namespace

void registerVendor()
{
	if (!getenv("MATCHFRAME_SELFTEST_FAKE_REPLAYCUE"))
		return;
	st().vendor = obs_websocket_register_vendor("ReplayCue");
	if (!st().vendor) {
		obs_log(LOG_WARNING, "[selftest] could not register the fake ReplayCue vendor");
		return;
	}
	struct {
		const char *name;
		obs_websocket_request_callback_function fn;
	} const reqs[] = {
		{"GetStatus", reqGetStatus},
		{"GetEvents", reqGetEvents},
		{"CueEvent", reqCueEvent},
		{"Resume", reqResume},
		{"PlayHighlights", reqPlayHighlights},
		{"ReturnToLive", reqReturnToLive},
		{"SelectEvent", reqSelectEvent},
		{"SetSpeed", reqSetSpeed},
		{"InstantReplay", reqInstant},
		{"MarkIn", reqMarkIn},
		{"MarkOut", reqMarkOut},
		{"PlayPause", reqPlayPause},
		{"NextAngle", reqNextAngle},
		{"PrevAngle", reqPrevAngle},
	};
	for (const auto &r : reqs)
		obs_websocket_vendor_register_request(st().vendor, r.name, r.fn, nullptr);
	obs_log(LOG_INFO, "[selftest] fake ReplayCue vendor registered");
}

int calls(const char *name)
{
	return st().calls[name];
}

void setReturnWorks(bool works)
{
	st().returnWorks = works;
}

double speed()
{
	return st().speed;
}

std::string selected()
{
	return st().selected;
}

bool highlightsPlaying()
{
	return st().highlights;
}

} // namespace mf::fakerc
