#pragma once

#include "providers/twitch.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

/* v0.4: central registry of connected OAuth providers.
   Exposes a uniform API for the multistream-dock UI + destination-dialog combo.

   Designed to grow with v0.5 (YouTube) and v0.6 (Kick). */
class PlatformManager : public QObject {
	Q_OBJECT
public:
	static PlatformManager &instance();

	struct RTMPTarget {
		QString rtmp_url;
		QString stream_key;
		QString error;
	};

	void connect_platform(const QString &platform_id, std::function<void(bool, const QString &error)> cb);
	void disconnect_platform(const QString &platform_id);
	bool is_connected(const QString &platform_id) const;
	QString display_name(const QString &platform_id) const;
	QStringList connected_platforms() const;

	void fetch_rtmp_target(const QString &platform_id, std::function<void(const RTMPTarget &)> cb);

	/* Called at module load — restores tokens for all providers. */
	void try_restore_all();

	/* v0.4.1: re-read oauth_apps.json (called after OAuth Settings dialog saves). */
	void reload_all_credentials();
	bool has_credentials(const QString &platform_id) const;

signals:
	void connection_changed(const QString &platform_id, bool connected);

private:
	PlatformManager();
	~PlatformManager() override = default;
	PlatformManager(const PlatformManager &) = delete;
	PlatformManager &operator=(const PlatformManager &) = delete;

	TwitchProvider *twitch_ = nullptr;
	/* YouTubeProvider *youtube_ = nullptr;   // v0.5 */
	/* KickProvider    *kick_    = nullptr;   // v0.6 */
};
