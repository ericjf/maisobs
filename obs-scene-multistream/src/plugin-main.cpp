#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"
#include "ui/multistream-dock.hpp"
#include "multistream-manager.hpp"
#include "oauth/platform-manager.hpp"

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-scene-multistream", "en-US")

static void frontend_event_cb(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		MultistreamManager::instance().stop_all();
	}
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "[scene-multistream] loading v%s", PLUGIN_VERSION);

	const auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		obs_log(LOG_ERROR, "[scene-multistream] frontend main window not available");
		return false;
	}

	auto *dock = new MultistreamDock(static_cast<QWidget *>(obs_frontend_get_main_window()));
	dock->setWindowTitle(QString::fromUtf8(obs_module_text("DockTitle")));

	obs_frontend_add_dock_by_id("obs-scene-multistream-dock", obs_module_text("DockTitle"), dock);

	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
	/* v0.3: subscribe SCENE_CHANGED for "follow OBS" outputs */
	MultistreamManager::instance().register_frontend_callback();
	/* v0.4: restore saved OAuth tokens (Twitch initially) */
	PlatformManager::instance().try_restore_all();
	return true;
}

void obs_module_unload(void)
{
	MultistreamManager::instance().unregister_frontend_callback();
	obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	MultistreamManager::instance().stop_all();
	obs_log(LOG_INFO, "[scene-multistream] unloaded");
}
