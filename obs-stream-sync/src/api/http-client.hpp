#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <functional>
#include <map>

/* Thin wrapper around QNetworkAccessManager for async HTTP calls.
   All callbacks fire on the Qt main thread. */
class HttpClient : public QObject {
	Q_OBJECT
public:
	using Headers = std::map<QString, QString>;
	using Callback = std::function<void(int status_code, const QByteArray &body, const QString &error)>;

	explicit HttpClient(QObject *parent = nullptr);

	void get(const QUrl &url, const Headers &headers, Callback cb);
	void post(const QUrl &url, const Headers &headers, const QByteArray &body, const QString &content_type,
		  Callback cb);
	void patch(const QUrl &url, const Headers &headers, const QByteArray &body, Callback cb);
	void put(const QUrl &url, const Headers &headers, const QByteArray &body, Callback cb);

private:
	void connect_reply(QNetworkReply *reply, Callback cb);

	QNetworkAccessManager *nam_;
};
