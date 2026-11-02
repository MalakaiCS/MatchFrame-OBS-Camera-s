#include "obs-util.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <cstdlib>
#include <cstring>

namespace mf {

std::string uuidOf(obs_source_t *source)
{
	const char *u = source ? obs_source_get_uuid(source) : nullptr;
	return u ? u : "";
}

std::string nameOf(obs_source_t *source)
{
	const char *n = source ? obs_source_get_name(source) : nullptr;
	return n ? n : "";
}

void markOwned(obs_source_t *source, const std::string &bankId, const std::string &role)
{
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	obs_data_set_string(priv, kOwnerKey, kOwnerValue);
	obs_data_set_string(priv, kBankKey, bankId.c_str());
	obs_data_set_string(priv, kRoleKey, role.c_str());
}

bool isOwned(obs_source_t *source, const std::string &bankId, const std::string &role)
{
	if (!source)
		return false;
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	return strcmp(obs_data_get_string(priv, kOwnerKey), kOwnerValue) == 0 &&
	       bankId == obs_data_get_string(priv, kBankKey) && role == obs_data_get_string(priv, kRoleKey);
}

bool isOwnedByAnyBank(obs_source_t *source)
{
	if (!source)
		return false;
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	return strcmp(obs_data_get_string(priv, kOwnerKey), kOwnerValue) == 0;
}

OBSSourceAutoRelease sourceByUuid(const std::string &uuid)
{
	if (uuid.empty())
		return nullptr;
	OBSSourceAutoRelease s = obs_get_source_by_uuid(uuid.c_str());
	// A removed (deleted) source can still be found while something holds a reference.
	if (s && obs_source_removed(s))
		return nullptr;
	return s;
}

namespace {
struct FindCtx {
	const std::string *bank;
	const std::string *role;
	obs_source_t *found = nullptr;
};
} // namespace

OBSSourceAutoRelease findOwned(const std::string &bankId, const std::string &role, bool scenes)
{
	FindCtx ctx{&bankId, &role};
	auto cb = [](void *param, obs_source_t *src) {
		auto *c = static_cast<FindCtx *>(param);
		if (!obs_source_removed(src) && isOwned(src, *c->bank, *c->role)) {
			c->found = obs_source_get_ref(src);
			return false;
		}
		return true;
	};
	if (scenes)
		obs_enum_scenes(cb, &ctx);
	else
		obs_enum_sources(cb, &ctx);
	return ctx.found;
}

std::string uniqueSourceName(const std::string &base)
{
	OBSSourceAutoRelease existing = obs_get_source_by_name(base.c_str());
	if (!existing)
		return base;
	for (int i = 2; i < 1000; i++) {
		std::string candidate = base + " (" + std::to_string(i) + ")";
		OBSSourceAutoRelease e = obs_get_source_by_name(candidate.c_str());
		if (!e)
			return candidate;
	}
	return base + " (" + std::to_string(os_gettime_ns()) + ")";
}

void canvasSize(uint32_t &w, uint32_t &h)
{
	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi)) {
		w = ovi.base_width;
		h = ovi.base_height;
	} else {
		w = 1920;
		h = 1080;
	}
}

double canvasAspect()
{
	uint32_t w, h;
	canvasSize(w, h);
	return (w && h) ? (double)w / (double)h : 16.0 / 9.0;
}

std::vector<obs_sceneitem_t *> sceneItems(obs_scene_t *scene)
{
	std::vector<obs_sceneitem_t *> items;
	if (!scene)
		return items;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			static_cast<std::vector<obs_sceneitem_t *> *>(param)->push_back(item);
			return true;
		},
		&items);
	return items;
}

int itemIndex(obs_scene_t *scene, obs_sceneitem_t *item)
{
	auto items = sceneItems(scene);
	for (size_t i = 0; i < items.size(); i++)
		if (items[i] == item)
			return (int)i;
	return -1;
}

obs_sceneitem_t *itemByIdAndSource(obs_scene_t *scene, int64_t id, obs_source_t *expect)
{
	if (!scene || id <= 0)
		return nullptr;
	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, id);
	if (!item)
		return nullptr;
	if (expect && obs_sceneitem_get_source(item) != expect)
		return nullptr;
	return item;
}

std::vector<obs_sceneitem_t *> itemsWithSource(obs_scene_t *scene, obs_source_t *source)
{
	std::vector<obs_sceneitem_t *> out;
	for (auto *item : sceneItems(scene))
		if (obs_sceneitem_get_source(item) == source)
			out.push_back(item);
	return out;
}

bool sourceTypeAvailable(const char *id)
{
	const char *t;
	for (size_t i = 0; obs_enum_input_types(i, &t); i++)
		if (strcmp(t, id) == 0)
			return true;
	return false;
}

const char *graphicsSourceTypeId()
{
#ifdef MF_ENABLE_SELFTEST
	const char *o = getenv("MATCHFRAME_TEST_GRAPHICS_SOURCE");
	if (o && *o)
		return o;
#endif
	return "browser_source";
}

OBSSourceAutoRelease findCutTransition()
{
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	obs_source_t *found = nullptr;
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *tr = list.sources.array[i];
		if (strcmp(obs_source_get_unversioned_id(tr), "cut_transition") == 0) {
			found = obs_source_get_ref(tr);
			break;
		}
	}
	obs_frontend_source_list_free(&list);
	return found;
}

OBSSourceAutoRelease findTransitionByName(const std::string &name)
{
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	obs_source_t *found = nullptr;
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *tr = list.sources.array[i];
		if (name == obs_source_get_name(tr)) {
			found = obs_source_get_ref(tr);
			break;
		}
	}
	obs_frontend_source_list_free(&list);
	return found;
}

std::string sceneTransitionOverride(obs_source_t *scene)
{
	if (!scene)
		return "";
	OBSDataAutoRelease priv = obs_source_get_private_settings(scene);
	const char *t = obs_data_get_string(priv, "transition");
	return t ? t : "";
}

int sceneTransitionDuration(obs_source_t *scene)
{
	if (!scene)
		return 300;
	OBSDataAutoRelease priv = obs_source_get_private_settings(scene);
	obs_data_set_default_int(priv, "transition_duration", 300); // OBS's own default
	return (int)obs_data_get_int(priv, "transition_duration");
}

void setSceneTransition(obs_source_t *scene, const std::string &name, int durationMs)
{
	if (!scene)
		return;
	OBSDataAutoRelease priv = obs_source_get_private_settings(scene);
	if (name.empty()) {
		obs_data_erase(priv, "transition");
		obs_data_erase(priv, "transition_duration");
		return;
	}
	obs_data_set_string(priv, "transition", name.c_str());
	obs_data_set_int(priv, "transition_duration", durationMs);
}

void clearTransitionOverride(obs_source_t *scene)
{
	setSceneTransition(scene, "", 0);
}

std::vector<std::string> transitionNames()
{
	std::vector<std::string> out;
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++)
		out.push_back(nameOf(list.sources.array[i]));
	obs_frontend_source_list_free(&list);
	return out;
}

bool transitionIsFixed(obs_source_t *transition)
{
	return transition && obs_transition_fixed(transition);
}

bool transitionIsCutType(obs_source_t *transition)
{
	return transition && strcmp(obs_source_get_unversioned_id(transition), "cut_transition") == 0;
}

std::string describeSceneTransition(obs_source_t *scene)
{
	const std::string name = sceneTransitionOverride(scene);
	OBSSourceAutoRelease tr = name.empty() ? nullptr : findTransitionByName(name);
	if (!tr || transitionIsCutType(tr))
		return "Cut"; // no setting, a deleted transition (OBS ignores it) or Cut itself
	if (transitionIsFixed(tr))
		return name;
	return name + " " + std::to_string(sceneTransitionDuration(scene)) + " ms";
}

bool transitionInProgress()
{
	OBSSourceAutoRelease tr = obs_get_output_source(0);
	if (!tr || obs_source_get_type(tr) != OBS_SOURCE_TYPE_TRANSITION)
		return false;
	float t = obs_transition_get_time(tr);
	return t > 0.0f && t < 1.0f;
}

static config_t *studioConfig()
{
#if LIBOBS_API_MAJOR_VER >= 31
	return obs_frontend_get_user_config();
#else
	return obs_frontend_get_global_config();
#endif
}

bool studioSceneDuplicationEnabled()
{
	config_t *c = studioConfig();
	return c ? config_get_bool(c, "BasicWindow", "SceneDuplicationMode") : true;
}

bool studioDuplicateSourcesEnabled()
{
	config_t *c = studioConfig();
	return c ? config_get_bool(c, "BasicWindow", "EditPropertiesMode") : false;
}

bool studioSwapScenesEnabled()
{
	config_t *c = studioConfig();
	return c ? config_get_bool(c, "BasicWindow", "SwapScenesMode") : true;
}

int64_t nowMs()
{
	return (int64_t)(os_gettime_ns() / 1000000ULL);
}

} // namespace mf
