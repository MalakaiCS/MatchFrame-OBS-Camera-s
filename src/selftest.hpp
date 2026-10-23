// MatchFrame Multiview - in-OBS automated self-test (test builds only).
//
// Built only with -DMF_ENABLE_SELFTEST=ON and active only when the environment
// variable MATCHFRAME_SELFTEST_OUT names an output JSON file. It drives the real
// plugin inside a real OBS session and then closes OBS.
#pragma once

#include <obs-frontend-api.h>

#include <string>

namespace mf {
void selftestOnEvent(enum obs_frontend_event event);

// Stand-in "ReplayCue" obs-websocket vendor (selftest-fake-replaycue.cpp).
namespace fakerc {
void registerVendor(); // call from obs_module_post_load
int calls(const char *requestType);
void setReturnWorks(bool works);
double speed();
std::string selected();
bool highlightsPlaying();
} // namespace fakerc
} // namespace mf
