#include "Net.h"
#include <QCoreApplication>
#include <QTimer>

namespace MI {

Net &Net::i()
{
    static Net n;
    return n;
}

Net::Net() {}

QNetworkRequest Net::makeRequest(const QUrl &url, const Headers &headers)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "ModInstaller/" APP_VERSION " (github.com/neil289506-commits/ModInstaller)");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);
    return req;
}

static QString describe(QNetworkReply *r)
{
    const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code)
        return QStringLiteral("HTTP %1 (%2)").arg(code).arg(r->errorString());
    return r->errorString();
}

void Net::getBytes(const QUrl &url, const Headers &headers, QObject *ctx, BytesCb cb, int retries)
{
    QPointer<QObject> guard(ctx);
    QNetworkReply *reply = m_nam.get(makeRequest(url, headers));
    connect(reply, &QNetworkReply::finished, this, [=, this]() {
        reply->deleteLater();
        if (ctx && guard.isNull())
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError) {
            cb(true, reply->readAll(), QString());
            return;
        }
        const bool transient = code == 0 || code == 429 || code >= 500;
        if (transient && retries > 0 && reply->error() != QNetworkReply::OperationCanceledError) {
            const QString err = describe(reply);
            Q_UNUSED(err)
            QTimer::singleShot(800, this, [=, this]() { getBytes(url, headers, ctx, cb, retries - 1); });
            return;
        }
        cb(false, reply->readAll(), describe(reply));
    });
}

void Net::postJson(const QUrl &url, const Headers &headers, const QByteArray &body, QObject *ctx, BytesCb cb)
{
    QPointer<QObject> guard(ctx);
    QNetworkRequest req = makeRequest(url, headers);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_nam.post(req, body);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        if (ctx && guard.isNull())
            return;
        if (reply->error() == QNetworkReply::NoError)
            cb(true, reply->readAll(), QString());
        else
            cb(false, reply->readAll(), describe(reply));
    });
}

} // namespace MI
