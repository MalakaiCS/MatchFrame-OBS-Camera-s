#include "scene-bank.hpp"

#include <plugin-support.h>

#include <algorithm>
#include <cstring>

namespace mf {

static const char *kBrowserCss = "body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }";

std::string SceneBank::sceneRole(int slot)
{
	if (slot == kSlotReplay)
		return "scene:replay";
	if (slot == kSlotHighlights)
		return "scene:highlights";
	if (slot == kSlotIntermission)
		return "scene:intermission";
	if (slot == kSlotAds)
		return "scene:ads";
	return "scene:cam" + std::to_string(slot + 1);
}

static const char *slotKey(int slot)
{
	switch (slot) {
	case kSlotReplay:
		return "replay";
	case kSlotIntermission:
		return "intermission";
	case kSlotAds:
		return "ads";
	default:
		return "highlights";
	}
}

std::string SceneBank::mediaRole(int slot)
{
	return std::string("media:") + slotKey(slot);
}

std::string SceneBank::playbackGraphicsRole(int slot)
{
	return std::string("graphics:") + slotKey(slot);
}

// Display names of the sources the plugin creates for slots 8..11.
static std::string mediaBaseName(int slot)
{
	switch (slot) {
	case kSlotReplay:
		return "MF — Replay Media";
	case kSlotAds:
		return "MF — Video Ads Media";
	default:
		return "MF — Highlights Media";
	}
}

static std::string graphicsBaseName(int slot)
{
	switch (slot) {
	case kSlotReplay:
		return "MF — Replay Graphics";
	case kSlotIntermission:
		return "MF — Intermission Graphics";
	case kSlotAds:
		return "MF — Video Ads Graphics";
	default:
		return "MF — Highlights Graphics";
	}
}

// ---------------------------------------------------------------------------
// resolution

static std::string &sceneUuidRef(BankConfig &c, int slot)
{
	return slot < 8 ? c.cameras[slot].sceneUuid : c.playback(slot).sceneUuid;
}

OBSSourceAutoRelease SceneBank::slotScene(int slot) const
{
	if (slot < 0 || slot >= kSlotCount)
		return nullptr;
	std::string &uuid = sceneUuidRef(cfg_, slot);
	const std::string role = sceneRole(slot);
	OBSSourceAutoRelease src = sourceByUuid(uuid);
	if (src && obs_scene_from_source(src) && isOwned(src, cfg_.bankId, role))
		return src;
	// Identity lost (e.g. config not yet saved): fall back to the ownership marker,
	// never to the name.
	OBSSourceAutoRelease owned = findOwned(cfg_.bankId, role, true);
	if (owned)
		uuid = uuidOf(owned);
	return owned;
}

OBSSourceAutoRelease SceneBank::cameraInput(int cam)
{
	if (cam < 0 || cam >= 8)
		return nullptr;
	CameraSlotConfig &c = cfg_.cameras[cam];
	if (c.inputUuid.empty() && c.inputName.empty())
		return nullptr;
	OBSSourceAutoRelease src = sourceByUuid(c.inputUuid);
	if (src && !obs_source_removed(src))
		return src;
	// Controlled name fallback: same name AND same input type, and not a plugin-owned
	// object. Used after migrating name-only configs; the UUID is recorded immediately.
	if (!c.inputName.empty()) {
		OBSSourceAutoRelease byName = obs_get_source_by_name(c.inputName.c_str());
		if (byName && !obs_source_removed(byName) && obs_source_get_type(byName) == OBS_SOURCE_TYPE_INPUT &&
		    !isOwnedByAnyBank(byName) &&
		    (c.inputTypeId.empty() || c.inputTypeId == obs_source_get_unversioned_id(byName))) {
			std::string newUuid = uuidOf(byName);
			if (!c.inputUuid.empty() && newUuid != c.inputUuid)
				return nullptr; // the original input is gone; a same-named new one is not it
			obs_log(LOG_INFO, "camera %d: linked input '%s' by name (type %s)", cam + 1,
				c.inputName.c_str(), obs_source_get_unversioned_id(byName));
			c.inputUuid = newUuid;
			c.inputTypeId = obs_source_get_unversioned_id(byName);
			return byName;
		}
	}
	return nullptr;
}

OBSSourceAutoRelease SceneBank::mediaSource(int slot) const
{
	if (!slotIsPlayback(slot))
		return nullptr;
	PlaybackSlotConfig &p = cfg_.playback(slot);
	OBSSourceAutoRelease src = sourceByUuid(p.mediaUuid);
	if (src && isOwned(src, cfg_.bankId, mediaRole(slot)))
		return src;
	OBSSourceAutoRelease owned = findOwned(cfg_.bankId, mediaRole(slot), false);
	if (owned)
		p.mediaUuid = uuidOf(owned);
	return owned;
}

OBSSourceAutoRelease SceneBank::cameraGraphics() const
{
	OBSSourceAutoRelease src = sourceByUuid(cfg_.graphicsUuid);
	if (src && isOwned(src, cfg_.bankId, kCameraGraphicsRole))
		return src;
	OBSSourceAutoRelease owned = findOwned(cfg_.bankId, kCameraGraphicsRole, false);
	if (owned)
		cfg_.graphicsUuid = uuidOf(owned);
	return owned;
}

OBSSourceAutoRelease SceneBank::playbackGraphics(int slot) const
{
	PlaybackSlotConfig &p = cfg_.playback(slot);
	OBSSourceAutoRelease src = sourceByUuid(p.graphicsUuid);
	if (src && isOwned(src, cfg_.bankId, playbackGraphicsRole(slot)))
		return src;
	OBSSourceAutoRelease owned = findOwned(cfg_.bankId, playbackGraphicsRole(slot), false);
	if (owned)
		p.graphicsUuid = uuidOf(owned);
	return owned;
}

int SceneBank::slotOfSceneUuid(const std::string &uuid) const
{
	if (uuid.empty())
		return -1;
	for (int i = 0; i < 8; i++)
		if (cfg_.cameras[i].sceneUuid == uuid)
			return i;
	if (cfg_.replay.sceneUuid == uuid)
		return kSlotReplay;
	if (cfg_.highlights.sceneUuid == uuid)
		return kSlotHighlights;
	if (cfg_.intermission.sceneUuid == uuid)
		return kSlotIntermission;
	if (cfg_.ads.sceneUuid == uuid)
		return kSlotAds;
	return -1;
}

int SceneBank::slotOfScene(obs_source_t *scene) const
{
	return scene ? slotOfSceneUuid(uuidOf(scene)) : -1;
}

bool SceneBank::anySceneExists() const
{
	for (int i = 0; i < kSlotCount; i++) {
		OBSSourceAutoRelease s = slotScene(i);
		if (s)
			return true;
	}
	return false;
}

obs_sceneitem_t *SceneBank::cameraItem(int cam, obs_scene_t *scene, obs_source_t *input) const
{
	if (!scene || !input)
		return nullptr;
	obs_sceneitem_t *item = itemByIdAndSource(scene, cfg_.cameras[cam].inputItemId, input);
	if (item)
		return item;
	auto v = itemsWithSource(scene, input);
	return v.empty() ? nullptr : v.front();
}

obs_sceneitem_t *SceneBank::graphicsItem(int cam, obs_scene_t *scene, obs_source_t *browser) const
{
	if (!scene || !browser)
		return nullptr;
	obs_sceneitem_t *item = itemByIdAndSource(scene, cfg_.cameras[cam].graphicsItemId, browser);
	if (item)
		return item;
	auto v = itemsWithSource(scene, browser);
	return v.empty() ? nullptr : v.front();
}

bool SceneBank::browserAvailable() const
{
	return sourceTypeAvailable(graphicsSourceTypeId());
}

bool SceneBank::mediaAvailable() const
{
	return sourceTypeAvailable("ffmpeg_source");
}

// ---------------------------------------------------------------------------
// settings for plugin-created sources

void SceneBank::applyBrowserDefaults(obs_data_t *s, const std::string &url, uint32_t w, uint32_t h)
{
	obs_data_set_bool(s, "is_local_file", false);
	obs_data_set_string(s, "url", url.c_str());
	obs_data_set_int(s, "width", w);
	obs_data_set_int(s, "height", h);
	obs_data_set_string(s, "css", kBrowserCss);
	obs_data_set_bool(s, "shutdown", false);            // keep running when not visible
	obs_data_set_bool(s, "restart_when_active", false); // never refresh on scene activation
	obs_data_set_bool(s, "reroute_audio", false);
	obs_data_set_bool(s, "fps_custom", false);
}

void SceneBank::applyMediaDefaults(obs_data_t *s)
{
	obs_data_set_bool(s, "is_local_file", true);
	obs_data_set_bool(s, "looping", false);
	obs_data_set_bool(s, "restart_on_activate", false); // the plugin controls starts explicitly
	obs_data_set_bool(s, "close_when_inactive", false); // keep the file loaded while off-air
	obs_data_set_bool(s, "clear_on_media_end", false);  // hold the last frame when ENDED
	obs_data_set_bool(s, "hw_decode", false);
}

void SceneBank::applyFramingToItem(obs_sceneitem_t *item, Framing framing)
{
	uint32_t w, h;
	canvasSize(w, h);
	vec2 bounds;
	vec2_set(&bounds, (float)w, (float)h);
	vec2 pos;
	vec2_set(&pos, 0.0f, 0.0f);
	vec2 scale;
	vec2_set(&scale, 1.0f, 1.0f);

	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_rot(item, 0.0f);
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_bounds_type(item, framing == Framing::Fit ? OBS_BOUNDS_SCALE_INNER : OBS_BOUNDS_SCALE_OUTER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 1, 0)
	obs_sceneitem_set_bounds_crop(item, framing == Framing::Fill);
#endif
	obs_sceneitem_defer_update_end(item);
}

// ---------------------------------------------------------------------------
// create / repair

// Each scene keeps the transition chosen for it in Setup. Only a setting that names a
// transition which no longer exists is removed (OBS would silently ignore it).
static void checkSceneTransition(obs_source_t *scene, RepairReport &r)
{
	const std::string name = sceneTransitionOverride(scene);
	if (name.empty())
		return;
	OBSSourceAutoRelease tr = findTransitionByName(name);
	if (tr)
		return;
	clearTransitionOverride(scene);
	r.warn("'" + nameOf(scene) + "' was set to transition '" + name +
	       "', which no longer exists - it now uses Cut (choose another in Setup → Transitions)");
}

OBSSourceAutoRelease SceneBank::resolveOrCreateScene(int slot, RepairReport &r)
{
	OBSSourceAutoRelease src = slotScene(slot);
	std::string &uuid = sceneUuidRef(cfg_, slot);
	if (src) {
		uuid = uuidOf(src);
		r.reused++;
		return src;
	}
	const std::string base = slot < 8 ? defaultCameraSceneName(slot) : defaultPlaybackSceneName(slot);
	const std::string name = uniqueSourceName(base);
	if (name != base)
		r.warn("'" + base + "' is used by an unrelated object; created '" + name + "' instead");
	obs_scene_t *scene = obs_scene_create(name.c_str());
	obs_source_t *s = obs_scene_get_source(scene);
	markOwned(s, cfg_.bankId, sceneRole(slot));
	uuid = uuidOf(s);
	if (slot < 8)
		cfg_.cameras[slot].sceneName = name;
	else
		cfg_.playback(slot).sceneName = name;
	r.created++;
	r.add("Created scene '" + name + "'");
	OBSSourceAutoRelease ret = obs_source_get_ref(s);
	obs_scene_release(scene); // the frontend's scene list holds its own reference
	return ret;
}

OBSSourceAutoRelease SceneBank::resolveOrCreateBrowser(const std::string &uuidIn, const std::string &role,
						       const std::string &baseName, const std::string &url,
						       RepairReport &r, std::string &uuidOut)
{
	uint32_t w, h;
	canvasSize(w, h);
	OBSSourceAutoRelease src = sourceByUuid(uuidIn);
	if (!(src && isOwned(src, cfg_.bankId, role)))
		src = findOwned(cfg_.bankId, role, false);

	if (src) {
		uuidOut = uuidOf(src);
		r.reused++;
		OBSDataAutoRelease cur = obs_source_get_settings(src);
		OBSDataAutoRelease fix = obs_data_create();
		bool changed = false, reloads = false;
		if (obs_data_get_bool(cur, "shutdown")) {
			obs_data_set_bool(fix, "shutdown", false);
			changed = reloads = true;
		}
		if (obs_data_get_bool(cur, "restart_when_active")) {
			obs_data_set_bool(fix, "restart_when_active", false);
			changed = reloads = true;
		}
		if ((uint32_t)obs_data_get_int(cur, "width") != w || (uint32_t)obs_data_get_int(cur, "height") != h) {
			// Size-only changes resize the page in place (no reload).
			obs_data_set_int(fix, "width", w);
			obs_data_set_int(fix, "height", h);
			changed = true;
			r.add("Resized '" + nameOf(src) + "' to the canvas " + std::to_string(w) + "x" +
			      std::to_string(h));
		}
		if (changed)
			obs_source_update(src, fix);
		if (reloads) {
			r.pageReloaded = true;
			r.warn("Corrected 'shutdown when not visible' / 'refresh when active' on '" + nameOf(src) +
			       "' - this reloaded that graphics page once");
		}
		return src;
	}

	OBSDataAutoRelease settings = obs_data_create();
	applyBrowserDefaults(settings, url, w, h);
	const std::string name = uniqueSourceName(baseName);
	if (name != baseName)
		r.warn("'" + baseName + "' is used by an unrelated object; created '" + name + "' instead");
	OBSSourceAutoRelease created = obs_source_create(graphicsSourceTypeId(), name.c_str(), settings, nullptr);
	if (!created) {
		r.warn("Could not create Browser Source '" + name + "'");
		return nullptr;
	}
	obs_source_set_monitoring_type(created, OBS_MONITORING_TYPE_NONE);
	markOwned(created, cfg_.bankId, role);
	uuidOut = uuidOf(created);
	r.created++;
	r.add("Created Browser Source '" + name + "' (" + redactUrl(url) + ")");
	return created;
}

OBSSourceAutoRelease SceneBank::resolveOrCreateMedia(int slot, RepairReport &r)
{
	PlaybackSlotConfig &p = cfg_.playback(slot);
	OBSSourceAutoRelease src = mediaSource(slot);
	if (src) {
		r.reused++;
		OBSDataAutoRelease cur = obs_source_get_settings(src);
		OBSDataAutoRelease fix = obs_data_create();
		bool changed = false;
		auto want = [&](const char *key, bool value) {
			if (obs_data_get_bool(cur, key) != value) {
				obs_data_set_bool(fix, key, value);
				changed = true;
			}
		};
		want("looping", false);
		want("restart_on_activate", false);
		want("close_when_inactive", false);
		want("clear_on_media_end", false);
		if (changed) {
			obs_source_update(src, fix);
			r.add("Restored playback settings on '" + nameOf(src) + "'");
		}
		return src;
	}
	OBSDataAutoRelease settings = obs_data_create();
	applyMediaDefaults(settings);
	obs_data_set_string(settings, "local_file", p.filePath.c_str());
	const std::string base = mediaBaseName(slot);
	const std::string name = uniqueSourceName(base);
	if (name != base)
		r.warn("'" + base + "' is used by an unrelated object; created '" + name + "' instead");
	OBSSourceAutoRelease created = obs_source_create("ffmpeg_source", name.c_str(), settings, nullptr);
	if (!created) {
		r.warn("Could not create Media Source '" + name + "'");
		return nullptr;
	}
	obs_source_set_monitoring_type(created, OBS_MONITORING_TYPE_NONE);
	markOwned(created, cfg_.bankId, mediaRole(slot));
	p.mediaUuid = uuidOf(created);
	r.created++;
	r.add("Created Media Source '" + name + "'");
	return created;
}

void SceneBank::repairCameraScene(int cam, obs_source_t *browser, RepairReport &r)
{
	OBSSourceAutoRelease sceneSrc = resolveOrCreateScene(cam, r);
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene)
		return;
	CameraSlotConfig &c = cfg_.cameras[cam];
	c.sceneName = nameOf(sceneSrc);

	checkSceneTransition(sceneSrc, r);

	obs_sceneitem_t *camItem = nullptr;
	OBSSourceAutoRelease input = cameraInput(cam);
	if (input) {
		camItem = cameraItem(cam, scene, input);
		if (!camItem) {
			camItem = obs_scene_add(scene, input);
			applyFramingToItem(camItem, Framing::Fit);
			obs_sceneitem_set_order(camItem, OBS_ORDER_MOVE_BOTTOM);
			r.created++;
			r.add("Added input '" + nameOf(input) + "' to '" + c.sceneName + "' (Fit)");
		}
		c.inputItemId = camItem ? obs_sceneitem_get_id(camItem) : 0;
		c.inputName = nameOf(input);
	} else if (!c.inputUuid.empty()) {
		r.warn("Camera " + std::to_string(cam + 1) + ": assigned input '" + c.inputName + "' no longer exists");
	}

	if (browser) {
		auto all = itemsWithSource(scene, browser);
		obs_sceneitem_t *gItem = graphicsItem(cam, scene, browser);
		if (all.size() > 1)
			r.warn("'" + c.sceneName + "' contains " + std::to_string(all.size()) +
			       " items of the shared graphics source; only the owned item is managed");
		if (!gItem) {
			gItem = obs_scene_add(scene, browser);
			obs_sceneitem_set_visible(gItem, cfg_.graphicsVisible);
			obs_sceneitem_set_locked(gItem, true);
			r.created++;
			r.add("Added shared graphics item to '" + c.sceneName + "'");
		}
		c.graphicsItemId = obs_sceneitem_get_id(gItem);
		if (camItem && itemIndex(scene, gItem) < itemIndex(scene, camItem)) {
			obs_sceneitem_set_order(gItem, OBS_ORDER_MOVE_TOP);
			r.add("Moved graphics above the camera in '" + c.sceneName + "'");
		}
	}
}

void SceneBank::repairPlaybackScene(int slot, RepairReport &r)
{
	OBSSourceAutoRelease sceneSrc = resolveOrCreateScene(slot, r);
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene)
		return;
	PlaybackSlotConfig &p = cfg_.playback(slot);
	p.sceneName = nameOf(sceneSrc);

	checkSceneTransition(sceneSrc, r);

	obs_sceneitem_t *mItem = nullptr;
	if (mediaAvailable()) {
		OBSSourceAutoRelease media = resolveOrCreateMedia(slot, r);
		if (media) {
			mItem = itemByIdAndSource(scene, p.mediaItemId, media);
			if (!mItem) {
				auto v = itemsWithSource(scene, media);
				mItem = v.empty() ? nullptr : v.front();
			}
			if (!mItem) {
				mItem = obs_scene_add(scene, media);
				applyFramingToItem(mItem, Framing::Fit);
				obs_sceneitem_set_order(mItem, OBS_ORDER_MOVE_BOTTOM);
				r.created++;
			}
			p.mediaItemId = obs_sceneitem_get_id(mItem);
		}
	} else {
		r.warn("Media Source (ffmpeg_source) is unavailable - playback disabled");
	}

	// Optional playback overlay: only when a URL is configured.
	OBSSourceAutoRelease pg;
	if (!p.graphicsUrl.empty() && browserAvailable()) {
		const std::string base = graphicsBaseName(slot);
		pg = resolveOrCreateBrowser(p.graphicsUuid, playbackGraphicsRole(slot), base, p.graphicsUrl, r,
					    p.graphicsUuid);
	} else {
		pg = playbackGraphics(slot);
	}
	if (pg) {
		obs_sceneitem_t *gItem = itemByIdAndSource(scene, p.graphicsItemId, pg);
		if (!gItem) {
			auto v = itemsWithSource(scene, pg);
			gItem = v.empty() ? nullptr : v.front();
		}
		if (!gItem) {
			gItem = obs_scene_add(scene, pg);
			obs_sceneitem_set_locked(gItem, true);
			r.created++;
		}
		obs_sceneitem_set_visible(gItem, p.graphicsEnabled && !p.graphicsUrl.empty());
		p.graphicsItemId = obs_sceneitem_get_id(gItem);
		if (mItem && itemIndex(scene, gItem) < itemIndex(scene, mItem))
			obs_sceneitem_set_order(gItem, OBS_ORDER_MOVE_TOP);
	}
}

// Intermission: one full-canvas Browser Source showing the intermission page. The
// camera overlay is never added here.
void SceneBank::repairIntermissionScene(RepairReport &r)
{
	OBSSourceAutoRelease sceneSrc = resolveOrCreateScene(kSlotIntermission, r);
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene)
		return;
	PlaybackSlotConfig &p = cfg_.intermission;
	p.sceneName = nameOf(sceneSrc);
	checkSceneTransition(sceneSrc, r);
	if (!browserAvailable()) {
		r.warn("Browser Source is not available - the Intermission scene has no graphics page");
		return;
	}
	OBSSourceAutoRelease page = resolveOrCreateBrowser(p.graphicsUuid, playbackGraphicsRole(kSlotIntermission),
							   graphicsBaseName(kSlotIntermission), p.graphicsUrl, r,
							   p.graphicsUuid);
	if (!page)
		return;
	if (p.graphicsUrl.empty())
		r.warn("No Intermission URL set yet - enter it in Setup and press Apply");
	obs_sceneitem_t *item = itemByIdAndSource(scene, p.graphicsItemId, page);
	if (!item) {
		auto v = itemsWithSource(scene, page);
		item = v.empty() ? nullptr : v.front();
	}
	if (!item) {
		item = obs_scene_add(scene, page);
		obs_sceneitem_set_locked(item, true);
		r.created++;
	}
	obs_sceneitem_set_visible(item, true);
	p.graphicsItemId = obs_sceneitem_get_id(item);
}

RepairReport SceneBank::createOrRepair()
{
	RepairReport r;
	OBSSourceAutoRelease browser;
	if (browserAvailable()) {
		browser = resolveOrCreateBrowser(cfg_.graphicsUuid, kCameraGraphicsRole, "MF — MatchFrame Overlay",
						 cfg_.graphicsUrl, r, cfg_.graphicsUuid);
		if (cfg_.graphicsUrl.empty())
			r.warn("No camera graphics URL set yet - enter the MatchFrame graphics output URL and press Apply URL");
	} else {
		r.warn("Browser Source is not available in this OBS installation - camera graphics disabled; "
		       "video-only switching and playback still work");
	}

	for (int cam = 0; cam < 8; cam++)
		repairCameraScene(cam, browser, r);
	repairPlaybackScene(kSlotReplay, r);
	repairPlaybackScene(kSlotHighlights, r);
	repairIntermissionScene(r);
	repairPlaybackScene(kSlotAds, r);

	obs_log(LOG_INFO, "Create/Repair: %d created, %d reused, %d warnings", r.created, r.reused, r.warnings);
	for (auto &l : r.lines)
		obs_log(LOG_INFO, "  %s", l.c_str());
	return r;
}

// ---------------------------------------------------------------------------
// assignments

bool SceneBank::assignInput(int cam, const std::string &inputUuid, std::string *message)
{
	if (cam < 0 || cam >= 8)
		return false;
	CameraSlotConfig &c = cfg_.cameras[cam];
	OBSSourceAutoRelease input;
	if (!inputUuid.empty()) {
		input = sourceByUuid(inputUuid);
		if (!input || obs_source_get_type(input) != OBS_SOURCE_TYPE_INPUT ||
		    !(obs_source_get_output_flags(input) & OBS_SOURCE_VIDEO) || isOwnedByAnyBank(input)) {
			if (message)
				*message = "Not a usable video input";
			return false;
		}
	}
	OBSSourceAutoRelease prev = cameraInput(cam);
	if (prev && input && prev.Get() == input.Get())
		return true; // unchanged: keep the existing item and its framing

	OBSSourceAutoRelease sceneSrc = slotScene(cam);
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (scene && c.inputItemId > 0) {
		obs_sceneitem_t *old = obs_scene_find_sceneitem_by_id(scene, c.inputItemId);
		if (old && (!input || obs_sceneitem_get_source(old) != input.Get()))
			obs_sceneitem_remove(old); // only the item this plugin recorded for the slot
	}

	c.inputUuid = input ? uuidOf(input) : "";
	c.inputName = input ? nameOf(input) : "";
	c.inputTypeId = input ? obs_source_get_unversioned_id(input) : "";
	c.inputItemId = 0;

	if (scene && input) {
		obs_sceneitem_t *item = cameraItem(cam, scene, input);
		if (!item) {
			item = obs_scene_add(scene, input);
			applyFramingToItem(item, Framing::Fit);
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
		}
		c.inputItemId = obs_sceneitem_get_id(item);
		OBSSourceAutoRelease browser = cameraGraphics();
		obs_sceneitem_t *g = graphicsItem(cam, scene, browser);
		if (g && itemIndex(scene, g) < itemIndex(scene, item))
			obs_sceneitem_set_order(g, OBS_ORDER_MOVE_TOP);
	} else if (input && message) {
		*message = "Assignment saved - run Create / Repair to build the camera scene";
	}
	obs_log(LOG_INFO, "camera %d assigned to '%s'", cam + 1, c.inputName.c_str());
	return true;
}

bool SceneBank::applyFraming(int cam, Framing framing, std::string *message)
{
	OBSSourceAutoRelease sceneSrc = slotScene(cam);
	OBSSourceAutoRelease input = cameraInput(cam);
	obs_sceneitem_t *item = cameraItem(cam, obs_scene_from_source(sceneSrc), input);
	if (!item) {
		if (message)
			*message = "Camera has no input item";
		return false;
	}
	applyFramingToItem(item, framing);
	return true;
}

std::vector<InputChoice> SceneBank::videoInputs() const
{
	std::vector<InputChoice> out;
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			auto *v = static_cast<std::vector<InputChoice> *>(param);
			if (obs_source_get_type(src) != OBS_SOURCE_TYPE_INPUT)
				return true;
			if (!(obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO))
				return true;
			if (isOwnedByAnyBank(src))
				return true;
			const char *id = obs_source_get_unversioned_id(src);
			if (!id || strcmp(id, "group") == 0 || strcmp(id, "scene") == 0)
				return true;
			v->push_back({uuidOf(src), nameOf(src), id});
			return true;
		},
		&out);
	std::sort(out.begin(), out.end(), [](const InputChoice &a, const InputChoice &b) { return a.name < b.name; });
	return out;
}

std::vector<int> SceneBank::slotsSharingInput(int cam) const
{
	std::vector<int> out;
	const std::string &u = cfg_.cameras[cam].inputUuid;
	if (u.empty())
		return out;
	for (int i = 0; i < 8; i++)
		if (i != cam && cfg_.cameras[i].inputUuid == u)
			out.push_back(i);
	return out;
}

void SceneBank::refreshNames()
{
	for (int i = 0; i < kSlotCount; i++) {
		OBSSourceAutoRelease s = slotScene(i);
		if (!s)
			continue;
		if (i < 8)
			cfg_.cameras[i].sceneName = nameOf(s);
		else
			cfg_.playback(i).sceneName = nameOf(s);
	}
	for (int i = 0; i < 8; i++) {
		OBSSourceAutoRelease in = cameraInput(i);
		if (in)
			cfg_.cameras[i].inputName = nameOf(in);
	}
}

} // namespace mf
