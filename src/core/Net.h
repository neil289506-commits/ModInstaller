#pragma once
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QUrl>
#include <functional>

namespace MI {

using Headers = QList<QPair<QByteArray, QByteArray>>;

class Net : public QObject
{
    Q_OBJECT
public:
    static Net &i();
    QNetworkAccessManager *nam() { return &m_nam; }

    // 建立帶有 User-Agent / 轉址策略的 request
    static QNetworkRequest makeRequest(const QUrl &url, const Headers &headers = {});

    // 取得位元組；失敗自動重試（網路錯誤、5xx、429），ctx 銷毀後不會再回呼
    using BytesCb = std::function<void(bool ok, const QByteArray &data, const QString &err)>;
    void getBytes(const QUrl &url, const Headers &headers, QObject *ctx, BytesCb cb, int retries = 2);

    void postJson(const QUrl &url, const Headers &headers, const QByteArray &body, QObject *ctx, BytesCb cb);

private:
    Net();
    QNetworkAccessManager m_nam;
};

} // namespace MI
