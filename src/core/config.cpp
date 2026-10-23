#include "config.hpp"

#include <obs-data.h>

#include <random>

namespace mf {

static const char *kCamSceneNames[8] = {
	"MF — CAM 01", "MF — CAM 02", "MF — CAM 03", "MF — CAM 04",
	"MF — CAM 05", "MF — CAM 06", "MF — CAM 07", "MF — CAM 08",
};
static const char *kCamLabels[8] = {
	"Main Wide", "Tight / Follow", "Left Goal",     "Right Goal",
	"Sideline",  "Reverse Angle",  "Bench / Crowd", "Handheld / Spare",
};

const char *defaultCameraSceneName(int cam)
{
	return (cam >= 0 && cam < 8) ? kCamSceneNames[cam] : "MF — CAM";
}

const char *defaultCameraLabel(int cam)
{
	return (cam >= 0 && cam < 8) ? kCamLabels[cam] : "";
}

const char *defaultPlaybackSceneName(int slot)
{
	return slot == 8 ? "MF — REPLAY" : "MF — HIGHLIGHTS";
}

const char *defaultPlaybackLabel(int slot)
{
	return slot == 8 ? "Replay" : "Half-Time / Full-Time Highlights";
}

std::string randomId()
{
	std::random_device rd;
	std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd());
	static const char hex[] = "0123456789abcdef";
	std::string s;
	for (int i = 0; i < 16; i++)
		s += hex[gen() & 15];
	return s;
}

BankConfig makeDefaultConfig(const std::string &bankId)
{
	BankConfig c;
	c.bankId = bankId;
	for (int i = 0; i < 8; i++) {
		c.cameras[i].label = kCamLabels[i];
		c.cameras[i].sceneName = kCamSceneNames[i];
	}
	c.replay.label = defaultPlaybackLabel(8);
	c.replay.sceneName = defaultPlaybackSceneName(8);
	c.highlights.label = defaultPlaybackLabel(9);
	c.highlights.sceneName = defaultPlaybackSceneName(9);
	return c;
}

static std::string str(obs_data_t *d, const char *k)
{
	const char *v = obs_data_get_string(d, k);
	return v ? v : "";
}

static void savePlayback(const PlaybackSlotConfig &p, obs_data_t *d)
{
	obs_data_set_string(d, "label", p.label.c_str());
	obs_data_set_string(d, "scene_uuid", p.sceneUuid.c_str());
	obs_data_set_string(d, "scene_name", p.sceneName.c_str());
	obs_data_set_string(d, "media_uuid", p.mediaUuid.c_str());
	obs_data_set_int(d, "media_item_id", p.mediaItemId);
	obs_data_set_string(d, "file_path", p.filePath.c_str());
	obs_data_set_string(d, "graphics_url", p.graphicsUrl.c_str());
	obs_data_set_bool(d, "graphics_enabled", p.graphicsEnabled);
	obs_data_set_string(d, "graphics_uuid", p.graphicsUuid.c_str());
	obs_data_set_int(d, "graphics_item_id", p.graphicsItemId);
}

static void loadPlayback(PlaybackSlotConfig &p, obs_data_t *d)
{
	if (!d)
		return;
	if (obs_data_has_user_value(d, "label"))
		p.label = str(d, "label");
	p.sceneUuid = str(d, "scene_uuid");
	if (obs_data_has_user_value(d, "scene_name"))
		p.sceneName = str(d, "scene_name");
	p.mediaUuid = str(d, "media_uuid");
	p.mediaItemId = obs_data_get_int(d, "media_item_id");
	p.filePath = str(d, "file_path");
	p.graphicsUrl = str(d, "graphics_url");
	p.graphicsEnabled = obs_data_get_bool(d, "graphics_enabled");
	p.graphicsUuid = str(d, "graphics_uuid");
	p.graphicsItemId = obs_data_get_int(d, "graphics_item_id");
}

void saveConfig(const BankConfig &cfg, obs_data_t *data)
{
	obs_data_set_int(data, "version", kConfigVersion);
	obs_data_set_string(data, "bank_id", cfg.bankId.c_str());
	obs_data_set_string(data, "graphics_url", cfg.graphicsUrl.c_str());
	obs_data_set_string(data, "graphics_uuid", cfg.graphicsUuid.c_str());
	obs_data_set_bool(data, "graphics_visible", cfg.graphicsVisible);
	obs_data_set_string(data, "last_live_camera_uuid", cfg.lastLiveCameraUuid.c_str());

	obs_data_array_t *cams = obs_data_array_create();
	for (const auto &c : cfg.cameras) {
		obs_data_t *d = obs_data_create();
		obs_data_set_string(d, "label", c.label.c_str());
		obs_data_set_string(d, "scene_uuid", c.sceneUuid.c_str());
		obs_data_set_string(d, "scene_name", c.sceneName.c_str());
		obs_data_set_string(d, "input_uuid", c.inputUuid.c_str());
		obs_data_set_string(d, "input_name", c.inputName.c_str());
		obs_data_set_string(d, "input_type", c.inputTypeId.c_str());
		obs_data_set_int(d, "input_item_id", c.inputItemId);
		obs_data_set_int(d, "graphics_item_id", c.graphicsItemId);
		obs_data_array_push_back(cams, d);
		obs_data_release(d);
	}
	obs_data_set_array(data, "cameras", cams);
	obs_data_array_release(cams);

	obs_data_t *r = obs_data_create();
	savePlayback(cfg.replay, r);
	obs_data_set_obj(data, "replay", r);
	obs_data_release(r);

	obs_data_t *h = obs_data_create();
	savePlayback(cfg.highlights, h);
	obs_data_set_obj(data, "highlights", h);
	obs_data_release(h);
}

// Schema v1 (spike builds) stored names only:
//   { "version": 1, "cameras": [ { "label", "scene", "input" } ], "graphics_url",
//     "replay_file", "highlights_file" }
// It is migrated to v2 by carrying names forward; identities are resolved (and
// recorded as UUIDs) by the scene bank on the next resolve / repair.
static void migrateV1(BankConfig &c, obs_data_t *data)
{
	obs_data_array_t *cams = obs_data_get_array(data, "cameras");
	size_t n = cams ? obs_data_array_count(cams) : 0;
	for (size_t i = 0; i < n && i < 8; i++) {
		obs_data_t *d = obs_data_array_item(cams, i);
		if (obs_data_has_user_value(d, "label"))
			c.cameras[i].label = str(d, "label");
		if (obs_data_has_user_value(d, "scene"))
			c.cameras[i].sceneName = str(d, "scene");
		c.cameras[i].inputName = str(d, "input");
		obs_data_release(d);
	}
	obs_data_array_release(cams);
	c.graphicsUrl = str(data, "graphics_url");
	c.replay.filePath = str(data, "replay_file");
	c.highlights.filePath = str(data, "highlights_file");
	c.migratedFromV1 = true;
}

BankConfig loadConfig(obs_data_t *data, const std::string &fallbackBankId)
{
	BankConfig c = makeDefaultConfig(fallbackBankId);
	if (!data)
		return c;

	int64_t version = obs_data_get_int(data, "version");
	if (version <= 0)
		return c; // nothing saved
	if (version == 1) {
		migrateV1(c, data);
		return c;
	}

	std::string bank = str(data, "bank_id");
	if (!bank.empty())
		c.bankId = bank;
	c.graphicsUrl = str(data, "graphics_url");
	c.graphicsUuid = str(data, "graphics_uuid");
	c.graphicsVisible =
		obs_data_has_user_value(data, "graphics_visible") ? obs_data_get_bool(data, "graphics_visible") : true;
	c.lastLiveCameraUuid = str(data, "last_live_camera_uuid");

	obs_data_array_t *cams = obs_data_get_array(data, "cameras");
	size_t n = cams ? obs_data_array_count(cams) : 0;
	for (size_t i = 0; i < n && i < 8; i++) {
		obs_data_t *d = obs_data_array_item(cams, i);
		CameraSlotConfig &s = c.cameras[i];
		if (obs_data_has_user_value(d, "label"))
			s.label = str(d, "label");
		s.sceneUuid = str(d, "scene_uuid");
		if (obs_data_has_user_value(d, "scene_name"))
			s.sceneName = str(d, "scene_name");
		s.inputUuid = str(d, "input_uuid");
		s.inputName = str(d, "input_name");
		s.inputTypeId = str(d, "input_type");
		s.inputItemId = obs_data_get_int(d, "input_item_id");
		s.graphicsItemId = obs_data_get_int(d, "graphics_item_id");
		obs_data_release(d);
	}
	obs_data_array_release(cams);

	obs_data_t *r = obs_data_get_obj(data, "replay");
	loadPlayback(c.replay, r);
	obs_data_release(r);
	obs_data_t *h = obs_data_get_obj(data, "highlights");
	loadPlayback(c.highlights, h);
	obs_data_release(h);
	return c;
}

std::string redactUrl(const std::string &url)
{
	if (url.empty())
		return url;
	std::string u = url;
	size_t cut = u.find_first_of("?#");
	bool hadQuery = cut != std::string::npos;
	if (hadQuery)
		u = u.substr(0, cut);
	size_t scheme = u.find("://");
	size_t hostStart = scheme == std::string::npos ? 0 : scheme + 3;
	size_t pathStart = u.find('/', hostStart);
	std::string authority =
		u.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
	size_t at = authority.rfind('@');
	if (at != std::string::npos)
		authority = "***@" + authority.substr(at + 1);
	std::string out = u.substr(0, hostStart) + authority;
	if (pathStart != std::string::npos) {
		// Keep only the first path segment; deeper segments may carry tokens.
		size_t next = u.find('/', pathStart + 1);
		out += u.substr(pathStart, next == std::string::npos ? std::string::npos : next - pathStart);
		if (next != std::string::npos && next + 1 < u.size())
			out += "/…";
	}
	if (hadQuery)
		out += "?…";
	return out;
}

} // namespace mf
