#include "graphics-controller.hpp"

#include <plugin-support.h>

namespace mf {

const char *graphicsVisibilityName(GraphicsVisibility v)
{
	switch (v) {
	case GraphicsVisibility::On:
		return "ON";
	case GraphicsVisibility::Off:
		return "OFF";
	case GraphicsVisibility::Mixed:
		return "MIXED";
	case GraphicsVisibility::NotSetUp:
		return "NOT SET UP";
	case GraphicsVisibility::Unavailable:
		return "UNAVAILABLE";
	}
	return "?";
}

GraphicsVisibility GraphicsController::visibility(int *itemCount) const
{
	if (!browserSupported())
		return GraphicsVisibility::Unavailable;
	OBSSourceAutoRelease browser = bank_.cameraGraphics();
	if (!browser)
		return GraphicsVisibility::NotSetUp;
	int on = 0, off = 0;
	for (int cam = 0; cam < 8; cam++) {
		OBSSourceAutoRelease scene = bank_.slotScene(cam);
		obs_sceneitem_t *item = bank_.graphicsItem(cam, obs_scene_from_source(scene), browser);
		if (!item)
			continue;
		(obs_sceneitem_visible(item) ? on : off)++;
	}
	if (itemCount)
		*itemCount = on + off;
	if (on + off == 0)
		return GraphicsVisibility::NotSetUp;
	if (on && off)
		return GraphicsVisibility::Mixed;
	return on ? GraphicsVisibility::On : GraphicsVisibility::Off;
}

int GraphicsController::setVisible(bool visible)
{
	OBSSourceAutoRelease browser = bank_.cameraGraphics();
	bank_.config().graphicsVisible = visible;
	if (!browser)
		return 0;
	int n = 0;
	for (int cam = 0; cam < 8; cam++) {
		OBSSourceAutoRelease scene = bank_.slotScene(cam);
		obs_scene_t *s = obs_scene_from_source(scene);
		obs_sceneitem_t *item = bank_.graphicsItem(cam, s, browser);
		if (!item)
			continue;
		bank_.config().cameras[cam].graphicsItemId = obs_sceneitem_get_id(item);
		obs_sceneitem_set_visible(item, visible);
		n++;
	}
	// The scene instance actually on Program may be a Studio Mode duplicate.
	OBSSourceAutoRelease tr = obs_get_output_source(0);
	if (tr && obs_source_get_type(tr) == OBS_SOURCE_TYPE_TRANSITION) {
		OBSSourceAutoRelease active = obs_transition_get_active_source(tr);
		obs_scene_t *programScene = obs_scene_from_source(active);
		if (programScene) {
			for (auto *item : itemsWithSource(programScene, browser))
				obs_sceneitem_set_visible(item, visible);
		}
	}
	obs_log(LOG_INFO, "camera graphics %s on %d camera scenes", visible ? "ON" : "OFF", n);
	return n;
}

bool GraphicsController::applyUrl(const std::string &url, std::string *message)
{
	bank_.config().graphicsUrl = url;
	OBSSourceAutoRelease browser = bank_.cameraGraphics();
	if (!browser) {
		if (message)
			*message = browserSupported() ? "URL saved - run Create / Repair to create the graphics source"
						      : "URL saved - Browser Source is unavailable in this OBS";
		return true;
	}
	OBSDataAutoRelease s = obs_data_create();
	obs_data_set_bool(s, "is_local_file", false);
	obs_data_set_string(s, "url", url.c_str());
	obs_source_update(browser, s);
	obs_log(LOG_INFO, "camera graphics URL applied: %s", redactUrl(url).c_str());
	if (message)
		*message = "Graphics URL applied - the page was reloaded";
	return true;
}

bool GraphicsController::setPlaybackOverlay(int slot, bool enabled, const std::string &url, std::string *message)
{
	PlaybackSlotConfig &p = bank_.config().playback(slot);
	bool urlChanged = url != p.graphicsUrl;
	p.graphicsEnabled = enabled;
	p.graphicsUrl = url;
	OBSSourceAutoRelease pg = bank_.playbackGraphics(slot);
	if (!pg) {
		if (message && !url.empty())
			*message = "Overlay saved - run Create / Repair to create it";
		return true;
	}
	if (urlChanged) {
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_string(s, "url", url.c_str());
		obs_source_update(pg, s);
		obs_log(LOG_INFO, "playback overlay URL applied: %s", redactUrl(url).c_str());
	}
	OBSSourceAutoRelease scene = bank_.slotScene(slot);
	obs_scene_t *sc = obs_scene_from_source(scene);
	obs_sceneitem_t *item = itemByIdAndSource(sc, p.graphicsItemId, pg);
	if (!item) {
		auto v = itemsWithSource(sc, pg);
		item = v.empty() ? nullptr : v.front();
	}
	if (item)
		obs_sceneitem_set_visible(item, enabled && !url.empty());
	return true;
}

} // namespace mf
