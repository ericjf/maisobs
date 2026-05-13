#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"
#include "ui/sync-dock.hpp"

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-stream-sync", "en-US")

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "[stream-sync] loading v%s", PLUGIN_VERSION);

	const auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		obs_log(LOG_ERROR, "[stream-sync] frontend main window not available");
		return false;
	}

	auto *dock = new SyncDock(static_cast<QWidget *>(obs_frontend_get_main_window()));
	dock->setWindowTitle(QString::fromUtf8(obs_module_text("DockTitle")));

	obs_frontend_add_dock_by_id("obs-stream-sync-dock", obs_module_text("DockTitle"), dock);

	obs_log(LOG_INFO, "[stream-sync] loaded");
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "[stream-sync] unloaded");
}
