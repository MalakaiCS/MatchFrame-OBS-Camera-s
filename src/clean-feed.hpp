// MatchFrame Multiview - "graphics on Program only".
//
// The shared camera graphics source stays in all eight camera scenes, so Program,
// recordings, streams and transitions are unchanged. A small pass-through filter on each
// plugin-owned graphics source draws nothing while the multiview is rendering one of its
// camera tiles or its Preview pane, so the operator sees clean cameras even when the
// overlay covers the picture (e.g. a full-screen break graphic).
#pragma once

#include <obs.h>

namespace mf {

constexpr const char *kCleanFeedFilterId = "matchframe_multiview_clean_feed";

void registerCleanFeedFilter();

// Global switch (Setup → "Graphics only on Program"). When off the filter is a no-op.
void setCleanFeedEnabled(bool enabled);
bool cleanFeedEnabled();

// Adds the filter to `source` if it is missing. Returns true when one was added.
bool ensureCleanFeedFilter(obs_source_t *source);
bool hasCleanFeedFilter(obs_source_t *source);

// Graphics-thread scope: while alive, graphics sources carrying the filter are hidden.
// The multiview's draw callback opens one around each tile and the Preview pane.
class CleanFeedScope {
public:
	CleanFeedScope();
	~CleanFeedScope();
	CleanFeedScope(const CleanFeedScope &) = delete;
	CleanFeedScope &operator=(const CleanFeedScope &) = delete;
};

} // namespace mf
