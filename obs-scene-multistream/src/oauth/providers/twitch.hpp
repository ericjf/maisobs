#pragma once

#include "../oauth-flow.hpp"
#include "../../api/http-client.hpp"
#include <QObject>
#include <QString>
#include <functional>

/* Twitch platform: OAuth + Helix API for channel title/category update. */
class TwitchProvider : public QObject {
	Q_OBJECT
public:
	/* Read from obs_module_config_path("twitch_app.json")
	   Format: { "client_id": "...", "client_secret": "..." } */
	explicit TwitchProvider(QObject *parent = nullptr);

	bool is_connected() const { return !access_token_.isEmpty(); }
	QString display_name() const { return display_name_; }

	/* Attempt to load saved tokens and validate them. Calls cb(true/false). */
	void try_restore(std::function<void(bool)> cb);

	/* v0.4.1: re-read oauth_apps.json (called after OAuth Settings dialog Save). */
	void reload_credentials();

	bool has_credentials() const { return !client_id_.isEmpty(); }

	/* Full OAuth flow — opens browser. */
	void connect(std::function<void(bool, const QString &error)> cb);

	/* Revoke token and clear storage. */
	void disconnect();

	/* Update live channel title and category (game name).
	   game_name may be empty to leave category unchanged. */
	void update_channel(const QString &title, const QString &game_name,
			    std::function<void(bool, const QString &error)> cb);

	/* v0.4: fetch RTMP ingest URL + stream key for current user.
	   Requires scope `channel:read:stream_key`. cb args: (ok, url, key, error). */
	void fetch_rtmp_target(std::function<void(bool, const QString &, const QString &, const QString &)> cb);

signals:
	void connection_changed(bool connected);

private:
	void fetch_user_info(std::function<void(bool)> cb);
	void resolve_game_id(const QString &game_name, std::function<void(const QString &game_id)> cb);
	void ensure_valid_token(std::function<void(bool)> cb);

	OAuthFlow *flow_;
	HttpClient *http_;

	QString client_id_;
	QString client_secret_;
	QString access_token_;
	QString refresh_token_;
	QString user_id_;
	QString display_name_;

	bool loading_config_ = false;
};
