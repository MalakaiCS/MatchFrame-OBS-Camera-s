/*
MatchFrame Multiview - OBS Studio plugin
Copyright (C) 2026 MatchFrame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "controller.hpp"
#ifdef MF_ENABLE_SELFTEST
#include "selftest.hpp"
#endif

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "MatchFrame Multiview: ten-scene sports switching with a native twelve-pane multiview.";
}

static mf::Controller *g_controller = nullptr;

static void frontendEvent(enum obs_frontend_event event, void *)
{
	if (g_controller)
		g_controller->onFrontendEvent(event);
#ifdef MF_ENABLE_SELFTEST
	mf::selftestOnEvent(event);
#endif
}

static void frontendSave(obs_data_t *data, bool saving, void *)
{
	if (g_controller)
		g_controller->onSave(data, saving);
}

bool obs_module_load(void)
{
	g_controller = new mf::Controller();
	g_controller->registerHotkeys();

	obs_frontend_add_event_callback(frontendEvent, nullptr);
	obs_frontend_add_save_callback(frontendSave, nullptr);

	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("MatchFrame.Menu")));
	QObject::connect(action, &QAction::triggered, []() {
		if (g_controller)
			g_controller->showMultiview();
	});

	obs_log(LOG_INFO, "plugin loaded successfully (version %s, built against libobs %d.%d.%d)", PLUGIN_VERSION,
		LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	obs_frontend_remove_save_callback(frontendSave, nullptr);
	delete g_controller;
	g_controller = nullptr;
	obs_log(LOG_INFO, "plugin unloaded");
}
