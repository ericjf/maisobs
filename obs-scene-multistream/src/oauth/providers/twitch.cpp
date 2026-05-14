#include "twitch.hpp"
#include "../token-store.hpp"
#include "../../plugin-support.h"

#include <obs-module.h>
#include <util/platform.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>

static constexpr const char *PROVIDER = "twitch";
static constexpr const char *AUTH_URL = "https://id.twitch.tv/oauth2/authorize";
static constexpr const char *TOKEN_URL = "https://id.twitch.tv/oauth2/token";
static constexpr const char *API_BASE = "https://api.twitch.tv/helix";

static OAuthFlow::Config make_config(const QString &client_id, const QString &client_secret)
{
	OAuthFlow::Config c;
	c.auth_url = AUTH_URL;
	c.token_url = TOKEN_URL;
	c.client_id = client_id;
	c.client_secret = client_secret;
	/* v0.4: + channel:read:stream_key for auto-populating Add destination dialog */
	c.scopes = {"channel:manage:broadcast", "user:read:email", "channel:read:stream_key"};
	return c;
}

TwitchProvider::TwitchProvider(QObject *parent)
	: QObject(parent),
	  flow_(new OAuthFlow(this)),
	  http_(new HttpClient(this))
{
	reload_credentials();
}

/* v0.4.1: re-read app credentials from oauth_apps.json. Called from ctor and
   from OAuth Settings dialog after user saves new client_id. */
void TwitchProvider::reload_credentials()
{
	client_id_.clear();
	client_secret_.clear();
	char *path = obs_module_config_path("oauth_apps.json");
	if (path && os_file_exists(path)) {
		obs_data_t *root = obs_data_create_from_json_file(path);
		if (root) {
			obs_data_t *t = obs_data_get_obj(root, "twitch");
			if (t) {
				client_id_ = obs_data_get_string(t, "client_id");
				client_secret_ = obs_data_get_string(t, "client_secret");
				obs_data_release(t);
			}
			obs_data_release(root);
		}
	}
	bfree(path);

	if (client_id_.isEmpty())
		obs_log(LOG_INFO, "[scene-multistream] Twitch client_id not configured yet");
	else
		obs_log(LOG_INFO, "[scene-multistream] Twitch credentials loaded (PKCE %s)",
			client_secret_.isEmpty() ? "only" : "+ secret");
}

/* v0.4: GET /helix/streams/key — returns RTMP ingest URL + stream key */
void TwitchProvider::fetch_rtmp_target(std::function<void(bool, const QString &, const QString &, const QString &)> cb)
{
	if (!is_connected()) {
		cb(false, {}, {}, "Twitch not connected");
		return;
	}
	if (user_id_.isEmpty()) {
		cb(false, {}, {}, "Twitch user_id not resolved");
		return;
	}
	QUrl url(QString(API_BASE) + "/streams/key");
	QUrlQuery q;
	q.addQueryItem("broadcaster_id", user_id_);
	url.setQuery(q);
	HttpClient::Headers h{{"Client-Id", client_id_}, {"Authorization", "Bearer " + access_token_}};
	http_->get(url, h, [this, cb](int status, const QByteArray &body, const QString &) {
		if (status == 401) {
			/* token expired or scope missing — try refresh once */
			ensure_valid_token([this, cb](bool ok) {
				if (!ok) {
					cb(false, {}, {}, "token refresh failed (re-connect to grant new scope)");
					return;
				}
				fetch_rtmp_target(cb);
			});
			return;
		}
		if (status != 200) {
			cb(false, {}, {}, QString("HTTP %1: %2").arg(status).arg(QString::fromUtf8(body)));
			return;
		}
		auto arr = QJsonDocument::fromJson(body).object()["data"].toArray();
		if (arr.isEmpty()) {
			cb(false, {}, {}, "no stream key returned");
			return;
		}
		QString key = arr[0].toObject()["stream_key"].toString();
		if (key.isEmpty()) {
			cb(false, {}, {}, "empty stream key");
			return;
		}
		/* Twitch ingest: rtmps://live.twitch.tv/app/<key>
		   Per-region ingest list at https://help.twitch.tv/s/twitch-ingest-recommendation
		   — generic edge auto-routes. */
		QString rtmp_url = "rtmps://live.twitch.tv/app";
		cb(true, rtmp_url, key, {});
	});
}

void TwitchProvider::try_restore(std::function<void(bool)> cb)
{
	std::string at, rt;
	if (!token_store::load(PROVIDER, at, rt)) {
		cb(false);
		return;
	}
	access_token_ = QString::fromStdString(at);
	refresh_token_ = QString::fromStdString(rt);
	fetch_user_info([this, cb](bool ok) {
		if (!ok) {
			/* Try refresh */
			ensure_valid_token([this, cb](bool ok2) {
				if (ok2)
					emit connection_changed(true);
				cb(ok2);
			});
		} else {
			emit connection_changed(true);
			cb(true);
		}
	});
}

void TwitchProvider::connect(std::function<void(bool, const QString &)> cb)
{
	if (client_id_.isEmpty()) {
		cb(false, "Twitch client_id not configured. See log for instructions.");
		return;
	}
	flow_->start(make_config(client_id_, client_secret_),
		     [this, cb](bool ok, const QString &at, const QString &rt, const QString &err) {
			     if (!ok) {
				     cb(false, err);
				     return;
			     }
			     access_token_ = at;
			     refresh_token_ = rt;
			     token_store::save(PROVIDER, at.toStdString(), rt.toStdString());
			     fetch_user_info([this, cb](bool user_ok) {
				     if (user_ok)
					     emit connection_changed(true);
				     cb(user_ok, user_ok ? QString{} : "Failed to fetch user info");
			     });
		     });
}

void TwitchProvider::disconnect()
{
	access_token_.clear();
	refresh_token_.clear();
	user_id_.clear();
	display_name_.clear();
	token_store::clear(PROVIDER);
	emit connection_changed(false);
}

void TwitchProvider::fetch_user_info(std::function<void(bool)> cb)
{
	HttpClient::Headers h{{"Client-Id", client_id_}, {"Authorization", "Bearer " + access_token_}};
	http_->get(QUrl(QString(API_BASE) + "/users"), h,
		   [this, cb](int status, const QByteArray &body, const QString &) {
			   if (status != 200) {
				   cb(false);
				   return;
			   }
			   auto obj = QJsonDocument::fromJson(body).object();
			   auto arr = obj["data"].toArray();
			   if (arr.isEmpty()) {
				   cb(false);
				   return;
			   }
			   auto user = arr[0].toObject();
			   user_id_ = user["id"].toString();
			   display_name_ = user["display_name"].toString();
			   obs_log(LOG_INFO, "[scene-multistream] Twitch connected: %s (id=%s)",
				   display_name_.toUtf8().constData(), user_id_.toUtf8().constData());
			   cb(true);
		   });
}

void TwitchProvider::ensure_valid_token(std::function<void(bool)> cb)
{
	if (refresh_token_.isEmpty()) {
		cb(false);
		return;
	}
	flow_->refresh(make_config(client_id_, client_secret_), refresh_token_,
		       [this, cb](bool ok, const QString &at, const QString &rt, const QString &) {
			       if (!ok) {
				       cb(false);
				       return;
			       }
			       access_token_ = at;
			       if (!rt.isEmpty())
				       refresh_token_ = rt;
			       token_store::save(PROVIDER, at.toStdString(), refresh_token_.toStdString());
			       fetch_user_info(cb);
		       });
}

void TwitchProvider::resolve_game_id(const QString &game_name, std::function<void(const QString &)> cb)
{
	QUrl url(QString(API_BASE) + "/games");
	QUrlQuery q;
	q.addQueryItem("name", game_name);
	url.setQuery(q);
	HttpClient::Headers h{{"Client-Id", client_id_}, {"Authorization", "Bearer " + access_token_}};
	http_->get(url, h, [cb](int status, const QByteArray &body, const QString &) {
		if (status != 200) {
			cb({});
			return;
		}
		auto arr = QJsonDocument::fromJson(body).object()["data"].toArray();
		if (arr.isEmpty()) {
			cb({});
			return;
		}
		cb(arr[0].toObject()["id"].toString());
	});
}

void TwitchProvider::update_channel(const QString &title, const QString &game_name,
				    std::function<void(bool, const QString &)> cb)
{
	if (!is_connected()) {
		cb(false, "Not connected");
		return;
	}

	auto do_update = [this, title, cb](const QString &game_id) {
		QUrl url(QString(API_BASE) + "/channels");
		QUrlQuery q;
		q.addQueryItem("broadcaster_id", user_id_);
		url.setQuery(q);

		QJsonObject body_obj;
		body_obj["title"] = title;
		if (!game_id.isEmpty())
			body_obj["game_id"] = game_id;
		QByteArray body = QJsonDocument(body_obj).toJson(QJsonDocument::Compact);

		HttpClient::Headers h{{"Client-Id", client_id_},
				      {"Authorization", "Bearer " + access_token_},
				      {"Content-Type", "application/json"}};
		http_->patch(
			url, h, body, [this, cb, title, body](int status, const QByteArray &resp, const QString &) {
				if (status == 204 || status == 200) {
					obs_log(LOG_INFO, "[scene-multistream] Twitch channel updated: %s",
						title.toUtf8().constData());
					cb(true, {});
				} else if (status == 401) {
					/* Token expired — refresh and retry once */
					ensure_valid_token([this, cb, title, body](bool ok) {
						if (!ok) {
							cb(false, "Token refresh failed");
							return;
						}
						update_channel(title, {}, cb);
					});
				} else {
					QString err = QString("HTTP %1: %2").arg(status).arg(QString::fromUtf8(resp));
					cb(false, err);
				}
			});
	};

	if (!game_name.isEmpty()) {
		resolve_game_id(game_name, [do_update, cb, game_name](const QString &game_id) {
			if (game_id.isEmpty()) {
				cb(false, "Game not found: " + game_name);
				return;
			}
			do_update(game_id);
		});
	} else {
		do_update({});
	}
}
