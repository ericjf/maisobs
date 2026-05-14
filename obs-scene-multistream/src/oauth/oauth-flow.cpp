#include "oauth-flow.hpp"
#include "../plugin-support.h"

#include <obs-module.h>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

static QString generate_random_base64url(int byte_count)
{
	QByteArray bytes(byte_count, '\0');
	for (int i = 0; i < byte_count; i++)
		bytes[i] = (char)(QRandomGenerator::global()->generate() & 0xFF);
	return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

static QString sha256_base64url(const QString &input)
{
	QByteArray hash = QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Sha256);
	return hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

OAuthFlow::OAuthFlow(QObject *parent) : QObject(parent) {}

void OAuthFlow::abort()
{
	if (server_) {
		server_->close();
		server_->deleteLater();
		server_ = nullptr;
	}
	pending_cb_ = nullptr;
}

void OAuthFlow::start(const Config &config, DoneCallback cb)
{
	abort();

	server_ = new QTcpServer(this);
	if (!server_->listen(QHostAddress::LocalHost, 0)) {
		cb(false, {}, {}, "Failed to start loopback server: " + server_->errorString());
		server_->deleteLater();
		server_ = nullptr;
		return;
	}

	quint16 port = server_->serverPort();
	redirect_uri_ = QString("http://127.0.0.1:%1/callback").arg(port);

	/* PKCE */
	code_verifier_ = generate_random_base64url(32);
	QString code_challenge = sha256_base64url(code_verifier_);
	state_ = generate_random_base64url(16);

	pending_config_ = config;
	pending_cb_ = std::move(cb);

	connect(server_, &QTcpServer::newConnection, this, &OAuthFlow::on_new_connection);

	QUrl url(config.auth_url);
	QUrlQuery q;
	q.addQueryItem("client_id", config.client_id);
	q.addQueryItem("redirect_uri", redirect_uri_);
	q.addQueryItem("response_type", "code");
	q.addQueryItem("scope", config.scopes.join(" "));
	q.addQueryItem("state", state_);
	q.addQueryItem("code_challenge", code_challenge);
	q.addQueryItem("code_challenge_method", "S256");
	url.setQuery(q);

	obs_log(LOG_INFO, "[scene-multistream] opening browser for OAuth: %s", url.toString().toUtf8().constData());
	QDesktopServices::openUrl(url);
}

void OAuthFlow::on_new_connection()
{
	QTcpSocket *socket = server_->nextPendingConnection();
	if (!socket)
		return;

	/* Read the HTTP request */
	connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
		QByteArray data = socket->readAll();
		QString request = QString::fromUtf8(data);

		/* Parse GET /callback?code=...&state=... */
		QString first_line = request.left(request.indexOf('\r'));
		int path_start = first_line.indexOf(' ') + 1;
		int path_end = first_line.lastIndexOf(' ');
		QString path = first_line.mid(path_start, path_end - path_start);

		QUrl url("http://localhost" + path);
		QUrlQuery query(url.query());
		QString code = query.queryItemValue("code");
		QString state = query.queryItemValue("state");
		QString error = query.queryItemValue("error");

		/* Send HTTP response */
		const char *html = "<html><body><h2>Authorization complete. You may close this tab.</h2></body></html>";
		QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
		resp += html;
		socket->write(resp);
		socket->flush();
		socket->disconnectFromHost();
		socket->deleteLater();

		server_->close();
		server_->deleteLater();
		server_ = nullptr;

		if (!error.isEmpty()) {
			if (pending_cb_)
				pending_cb_(false, {}, {}, "Authorization denied: " + error);
			pending_cb_ = nullptr;
			return;
		}

		if (state != state_) {
			if (pending_cb_)
				pending_cb_(false, {}, {}, "State mismatch — possible CSRF");
			pending_cb_ = nullptr;
			return;
		}

		if (code.isEmpty()) {
			if (pending_cb_)
				pending_cb_(false, {}, {}, "No code in callback");
			pending_cb_ = nullptr;
			return;
		}

		exchange_code(pending_config_, code, std::move(pending_cb_));
		pending_cb_ = nullptr;
	});
}

void OAuthFlow::exchange_code(const Config &config, const QString &code, DoneCallback cb)
{
	auto *nam = new QNetworkAccessManager(this);
	QNetworkRequest req(QUrl(config.token_url));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

	QUrlQuery body;
	body.addQueryItem("client_id", config.client_id);
	body.addQueryItem("code", code);
	body.addQueryItem("code_verifier", code_verifier_);
	body.addQueryItem("grant_type", "authorization_code");
	body.addQueryItem("redirect_uri", redirect_uri_);
	if (!config.client_secret.isEmpty())
		body.addQueryItem("client_secret", config.client_secret);

	QNetworkReply *reply = nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [reply, nam, cb = std::move(cb)]() {
		QByteArray data = reply->readAll();
		reply->deleteLater();
		nam->deleteLater();

		QJsonObject obj = QJsonDocument::fromJson(data).object();
		if (obj.contains("access_token")) {
			cb(true, obj["access_token"].toString(), obj.value("refresh_token").toString(), {});
		} else {
			QString err = obj.value("message").toString();
			if (err.isEmpty())
				err = obj.value("error_description").toString();
			if (err.isEmpty())
				err = QString::fromUtf8(data);
			cb(false, {}, {}, err);
		}
	});
}

void OAuthFlow::refresh(const Config &config, const QString &refresh_token, DoneCallback cb)
{
	auto *nam = new QNetworkAccessManager(this);
	QNetworkRequest req(QUrl(config.token_url));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

	QUrlQuery body;
	body.addQueryItem("client_id", config.client_id);
	body.addQueryItem("grant_type", "refresh_token");
	body.addQueryItem("refresh_token", refresh_token);
	if (!config.client_secret.isEmpty())
		body.addQueryItem("client_secret", config.client_secret);

	QNetworkReply *reply = nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [reply, nam, cb = std::move(cb)]() {
		QByteArray data = reply->readAll();
		reply->deleteLater();
		nam->deleteLater();

		QJsonObject obj = QJsonDocument::fromJson(data).object();
		if (obj.contains("access_token")) {
			cb(true, obj["access_token"].toString(), obj.value("refresh_token").toString(), {});
		} else {
			QString err = obj.value("message").toString();
			if (err.isEmpty())
				err = obj.value("error_description").toString();
			if (err.isEmpty())
				err = QString::fromUtf8(data);
			cb(false, {}, {}, err);
		}
	});
}
