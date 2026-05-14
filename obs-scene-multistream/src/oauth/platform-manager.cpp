#include "platform-manager.hpp"
#include "../plugin-support.h"

#include <obs-module.h>

PlatformManager &PlatformManager::instance()
{
	static PlatformManager inst;
	return inst;
}

PlatformManager::PlatformManager() : QObject(nullptr)
{
	twitch_ = new TwitchProvider(this);
	QObject::connect(twitch_, &TwitchProvider::connection_changed, this,
			 [this](bool connected) { emit connection_changed("twitch", connected); });
}

void PlatformManager::try_restore_all()
{
	if (twitch_)
		twitch_->try_restore([](bool ok) {
			obs_log(LOG_INFO, "[scene-multistream] Twitch token restore: %s", ok ? "ok" : "none");
		});
}

void PlatformManager::connect_platform(const QString &platform_id, std::function<void(bool, const QString &)> cb)
{
	if (platform_id == "twitch") {
		if (!twitch_) {
			cb(false, "Twitch provider not initialized");
			return;
		}
		twitch_->connect(cb);
		return;
	}
	cb(false, "Unknown platform: " + platform_id);
}

void PlatformManager::disconnect_platform(const QString &platform_id)
{
	if (platform_id == "twitch" && twitch_)
		twitch_->disconnect();
}

bool PlatformManager::is_connected(const QString &platform_id) const
{
	if (platform_id == "twitch")
		return twitch_ && twitch_->is_connected();
	return false;
}

QString PlatformManager::display_name(const QString &platform_id) const
{
	if (platform_id == "twitch" && twitch_)
		return twitch_->display_name();
	return {};
}

QStringList PlatformManager::connected_platforms() const
{
	QStringList result;
	if (twitch_ && twitch_->is_connected())
		result << "twitch";
	return result;
}

void PlatformManager::fetch_rtmp_target(const QString &platform_id, std::function<void(const RTMPTarget &)> cb)
{
	if (platform_id == "twitch") {
		if (!twitch_) {
			cb({{}, {}, "Twitch provider not initialized"});
			return;
		}
		twitch_->fetch_rtmp_target([cb](bool ok, const QString &url, const QString &key, const QString &err) {
			RTMPTarget t;
			if (ok) {
				t.rtmp_url = url;
				t.stream_key = key;
			} else {
				t.error = err;
			}
			cb(t);
		});
		return;
	}
	cb({{}, {}, "Unknown platform: " + platform_id});
}
