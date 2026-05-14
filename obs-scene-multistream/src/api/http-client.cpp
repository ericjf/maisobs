#include "http-client.hpp"

#include <QNetworkRequest>

HttpClient::HttpClient(QObject *parent) : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

static QNetworkRequest make_request(const QUrl &url, const HttpClient::Headers &headers)
{
	QNetworkRequest req(url);
	for (const auto &kv : headers)
		req.setRawHeader(kv.first.toUtf8(), kv.second.toUtf8());
	return req;
}

void HttpClient::connect_reply(QNetworkReply *reply, Callback cb)
{
	connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		QByteArray body = reply->readAll();
		QString err;
		if (reply->error() != QNetworkReply::NoError)
			err = reply->errorString();
		reply->deleteLater();
		cb(status, body, err);
	});
}

void HttpClient::get(const QUrl &url, const Headers &headers, Callback cb)
{
	connect_reply(nam_->get(make_request(url, headers)), std::move(cb));
}

void HttpClient::post(const QUrl &url, const Headers &headers, const QByteArray &body, const QString &content_type,
		      Callback cb)
{
	auto req = make_request(url, headers);
	req.setHeader(QNetworkRequest::ContentTypeHeader, content_type);
	connect_reply(nam_->post(req, body), std::move(cb));
}

void HttpClient::patch(const QUrl &url, const Headers &headers, const QByteArray &body, Callback cb)
{
	auto req = make_request(url, headers);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	connect_reply(nam_->sendCustomRequest(req, "PATCH", body), std::move(cb));
}

void HttpClient::put(const QUrl &url, const Headers &headers, const QByteArray &body, Callback cb)
{
	auto req = make_request(url, headers);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	connect_reply(nam_->put(req, body), std::move(cb));
}
