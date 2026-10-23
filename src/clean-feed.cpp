// MatchFrame Multiview - "graphics on Program only" filter.
#include "clean-feed.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <atomic>
#include <cstring>

namespace mf {

namespace {

std::atomic<bool> g_enabled{true};
// Every multiview, OBS display and output renders on the one graphics thread, one after
// another, so a thread-local depth marks exactly the multiview's own tile renders.
thread_local int t_cleanDepth = 0;

const char *filterName(void *)
{
	return obs_module_text("MatchFrame.CleanFeedFilter");
}

void *filterCreate(obs_data_t *, obs_source_t *source)
{
	return source;
}

void filterDestroy(void *) {}

void filterRender(void *data, gs_effect_t *)
{
	if (t_cleanDepth > 0 && g_enabled.load(std::memory_order_relaxed))
		return; // multiview tile / Preview pane: draw nothing
	obs_source_skip_video_filter(static_cast<obs_source_t *>(data));
}

void findFilter(obs_source_t *, obs_source_t *filter, void *param)
{
	if (strcmp(obs_source_get_unversioned_id(filter), kCleanFeedFilterId) == 0)
		*static_cast<bool *>(param) = true;
}

} // namespace

void registerCleanFeedFilter()
{
	obs_source_info info = {};
	info.id = kCleanFeedFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CAP_DISABLED; // added by the plugin only
	info.get_name = filterName;
	info.create = filterCreate;
	info.destroy = filterDestroy;
	info.video_render = filterRender;
	obs_register_source(&info);
}

void setCleanFeedEnabled(bool enabled)
{
	if (g_enabled.exchange(enabled) != enabled)
		obs_log(LOG_INFO, "camera graphics: %s",
			enabled ? "Program only (multiview tiles clean)" : "also shown in multiview tiles");
}

bool cleanFeedEnabled()
{
	return g_enabled.load();
}

bool hasCleanFeedFilter(obs_source_t *source)
{
	bool found = false;
	if (source)
		obs_source_enum_filters(source, findFilter, &found);
	return found;
}

bool ensureCleanFeedFilter(obs_source_t *source)
{
	if (!source || hasCleanFeedFilter(source))
		return false;
	OBSSourceAutoRelease filter =
		obs_source_create(kCleanFeedFilterId, obs_module_text("MatchFrame.CleanFeedFilter"), nullptr, nullptr);
	if (!filter)
		return false;
	obs_source_filter_add(source, filter);
	return true;
}

CleanFeedScope::CleanFeedScope()
{
	t_cleanDepth++;
}

CleanFeedScope::~CleanFeedScope()
{
	t_cleanDepth--;
}

} // namespace mf
