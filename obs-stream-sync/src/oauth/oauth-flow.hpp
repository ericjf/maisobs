#pragma once

#include <QObject>
#include <QString>
#include <QTcpServer>
#include <functional>

/* RFC 8252 loopback OAuth 2.0 flow with PKCE.
   Opens the system browser to the authorization URL, then listens on
   127.0.0.1 for the redirect callback and exchanges the code for tokens. */
class OAuthFlow : public QObject {
	Q_OBJECT
public:
	struct Config {
		QString auth_url;       /* e.g. https://id.twitch.tv/oauth2/authorize */
		QString token_url;      /* e.g. https://id.twitch.tv/oauth2/token */
		QString client_id;
		QString client_secret;  /* empty for PKCE-only (public clients) */
		QStringList scopes;
	};

	using DoneCallback = std::function<void(bool success, const QString &access_token,
						const QString &refresh_token, const QString &error)>;

	explicit OAuthFlow(QObject *parent = nullptr);

	/* Start the OAuth flow. Opens browser; calls cb when done or on error.
	   Only one flow at a time — call abort() before starting another. */
	void start(const Config &config, DoneCallback cb);
	void abort();

	/* Exchange a refresh_token for a new access_token + refresh_token. */
	void refresh(const Config &config, const QString &refresh_token, DoneCallback cb);

private slots:
	void on_new_connection();

private:
	void exchange_code(const Config &config, const QString &code, DoneCallback cb);

	QTcpServer *server_ = nullptr;
	QString state_;
	QString code_verifier_;
	QString redirect_uri_;
	Config pending_config_;
	DoneCallback pending_cb_;
};
