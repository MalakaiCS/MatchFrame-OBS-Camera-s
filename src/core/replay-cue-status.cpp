#include "replay-cue-status.hpp"

#include "playback-logic.hpp"

#include <obs-data.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace mf {

static std::string str(obs_data_t *d, const char *k)
{
	const char *v = d ? obs_data_get_string(d, k) : nullptr;
	return v ? v : "";
}

ReplayCueStatus parseReplayCueStatus(obs_data_t *data)
{
	ReplayCueStatus s;
	if (!data)
		return s;
	if (obs_data_has_user_value(data, "ok") && !obs_data_get_bool(data, "ok")) {
		s.error = str(data, "error");
		return s;
	}
	s.ok = true;
	s.armed = obs_data_get_bool(data, "armed");
	s.pendingReplays = (int)obs_data_get_int(data, "pending_replays");
	s.selectedEvent = str(data, "selected_event");

	obs_data_t *pb = obs_data_get_obj(data, "playback");
	if (pb) {
		std::string st = str(pb, "state");
		if (!st.empty())
			s.state = st;
		s.eventId = str(pb, "event_id");
		s.eventName = str(pb, "event_name");
		s.angle = (int)obs_data_get_int(pb, "angle");
		s.angleCount = (int)obs_data_get_int(pb, "angle_count");
		s.angleLabel = str(pb, "angle_label");
		if (obs_data_has_user_value(pb, "speed"))
			s.speed = obs_data_get_double(pb, "speed");
		s.position = obs_data_get_double(pb, "position");
		s.duration = obs_data_get_double(pb, "duration");
		s.cued = obs_data_get_bool(pb, "cued");
		s.highlights = obs_data_get_bool(pb, "highlights");
		obs_data_release(pb);
	}
	obs_data_t *m = obs_data_get_obj(data, "match");
	if (m) {
		s.matchTitle = str(m, "title");
		s.phaseName = str(m, "phase_name");
		s.inBreak = obs_data_get_bool(m, "in_break");
		obs_data_release(m);
	}
	return s;
}

int parseReplayCueEventCount(obs_data_t *data)
{
	if (!data)
		return -1;
	obs_data_array_t *events = obs_data_get_array(data, "events");
	if (!events)
		return -1;
	int n = (int)obs_data_array_count(events);
	obs_data_array_release(events);
	return n;
}

std::string describeReplayCuePlayback(const ReplayCueStatus &s)
{
	if (!s.ok)
		return s.error.empty() ? "Replay Cue not available" : s.error;
	std::string state = s.state;
	std::transform(state.begin(), state.end(), state.begin(),
		       [](unsigned char c) { return (char)std::toupper(c); });
	if (s.cued && s.state != "playing")
		state = "CUED";
	if (s.eventId.empty() && s.eventName.empty())
		return s.armed ? "ARMED · no replay loaded" : "IDLE · not armed";
	std::string out = state;
	if (!s.eventName.empty())
		out += " " + s.eventName;
	if (!s.angleLabel.empty())
		out += " · " + s.angleLabel;
	else if (s.angle > 0)
		out += " · angle " + std::to_string(s.angle);
	if (std::fabs(s.speed - 1.0) > 0.005) {
		char buf[16];
		snprintf(buf, sizeof(buf), " · %d%%", (int)std::lround(s.speed * 100));
		out += buf;
	}
	if (s.duration > 0)
		out += " · " + formatClock((int64_t)(s.position * 1000)) + "/" +
		       formatClock((int64_t)(s.duration * 1000));
	return out;
}

std::pair<std::string, std::string> pickReplayCueScenes(const std::vector<SceneCandidate> &scenes,
							const std::string &configuredReplay,
							const std::string &configuredHighlights)
{
	auto exists = [&](const std::string &n) {
		return !n.empty() &&
		       std::any_of(scenes.begin(), scenes.end(), [&](const SceneCandidate &c) { return c.name == n; });
	};
	std::string replay;
	if (exists(configuredReplay))
		replay = configuredReplay;
	else if (exists(kReplayCueDefaultScene))
		replay = kReplayCueDefaultScene;
	else {
		for (const auto &c : scenes)
			if (c.hasPlayer) {
				replay = c.name;
				break;
			}
	}

	std::string highlights;
	if (exists(configuredHighlights)) {
		highlights = configuredHighlights;
	} else {
		std::vector<std::string> others;
		for (const auto &c : scenes)
			if (c.hasPlayer && c.name != replay)
				others.push_back(c.name);
		// Exactly one other scene with the player: that is Replay Cue's highlights scene.
		highlights = others.size() == 1 ? others.front() : replay;
	}
	return {replay, highlights};
}

} // namespace mf
