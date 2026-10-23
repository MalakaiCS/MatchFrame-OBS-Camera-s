// MatchFrame Multiview - in-OBS automated self-test (test builds only).
//
// Built only with -DMF_ENABLE_SELFTEST=ON and active only when the environment
// variable MATCHFRAME_SELFTEST_OUT names an output JSON file. It drives the real
// plugin inside a real OBS session and then closes OBS.
#pragma once

#include <obs-frontend-api.h>

namespace mf {
void selftestOnEvent(enum obs_frontend_event event);
}
