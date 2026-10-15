// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "DeviceCode.h"
#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>

namespace rtc {
class WebSocket;
}

namespace MI {

struct IncomingRequest {
    QString id;       // 連線 ID
    QString fromCode; // 對方的 12 碼代碼
    QString fileName;
    qint64 size = 0;
};

// 模組包傳輸：以 PeerJS 雲端伺服器（預設 0.peerjs.com）交換訊號，實際檔案走 WebRTC 點對點傳輸。
// 流程：傳送方送出 Request（夾帶檔名與大小）→ 接收方左下角出現提醒 → 按「接受」後才建立連線並傳檔；
// 按「拒絕」會通知傳送方。接收方在按下接受之前不會回應連線，所以不會洩漏 IP。
class PeerService : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Registering, Connecting, Online, Offline, Error };

    static PeerService &i();

    void start();   // 讀取 / 產生代碼 → 向 Cloudflare 註冊 → 連線到 PeerJS
    void restart(); // 設定變更後重新連線
    void stop();

    QString code() const { return m_rec.code; }
    bool isRegistered() const { return m_rec.registered; }
    State state() const { return m_state; }
    QString stateText() const;
    bool isOnline() const { return m_state == State::Online; }

    // 傳送檔案（回傳傳輸 ID；失敗回傳空字串並以 error 說明）
    QString sendFile(const QString &peerCode, const QString &path, QString *error);
    void cancel(const QString &id);
    // 接收
    void accept(const QString &requestId, const QString &savePath);
    void reject(const QString &requestId);

    // 向 Cloudflare 查詢代碼是否存在（未設定註冊服務時直接視為存在）
    using ExistsCb = std::function<void(bool known, bool exists)>;
    void checkCodeExists(const QString &code, QObject *ctx, ExistsCb cb);

    static QString registryUrl();

signals:
    void stateChanged();
    void codeChanged(const QString &code);
    void incomingRequest(const MI::IncomingRequest &req);
    void requestExpired(const QString &id);
    void sendStatus(const QString &id, const QString &text);                       // 例如「等待對方接受…」
    void sendProgress(const QString &id, qint64 done, qint64 total);
    void sendFinished(const QString &id, bool ok, const QString &reason);           // reason: rejected/offline/timeout/cancelled/busy/unsupported/failed:...
    void recvProgress(const QString &id, qint64 done, qint64 total);
    void recvFinished(const QString &id, bool ok, const QString &message);

private:
    explicit PeerService(QObject *parent = nullptr);
    ~PeerService() override;
    struct Conn;
    using ConnPtr = std::shared_ptr<Conn>;

    void setState(State s, const QString &detail = QString());
    void ensureRegistered();
    void registerCode(int attempt);
    void connectSignaling();
    void closeSignaling();
    void scheduleReconnect();
    void onSignal(const QString &text);
    void sendSignal(const QString &type, const QString &dstPeerId, const QJsonObject &payload);

    void handleOffer(const QString &srcPeerId, const QJsonObject &payload);
    void handleAnswer(const QString &srcPeerId, const QJsonObject &payload);
    void handleCandidate(const QString &srcPeerId, const QJsonObject &payload);
    void handleLeave(const QString &srcPeerId, const QJsonObject &payload);
    void handleExpire(const QString &srcPeerId);

    ConnPtr makeConn(const QString &id, bool sender);
    void setupPeerConnection(const ConnPtr &c);
    void onDataOpen(const QString &id);
    void onDataText(const QString &id, const QString &text);
    void onDataBinary(const QString &id, const QByteArray &data);
    void onConnLost(const QString &id, const QString &why);
    void pump(const QString &id);
    void failConn(const QString &id, const QString &reason);
    void finishConn(const QString &id);
    void removeConn(const QString &id);

    static QString peerIdFor(const QString &code);
    static QString codeFromPeerId(const QString &peerId);
    static QString peerBaseUrl();
    static QString writeCaFile();

    DeviceCode::Record m_rec;
    State m_state = State::Idle;
    QString m_stateDetail;
    bool m_running = false;
    int m_generation = 0;
    int m_reconnectAttempt = 0;
    std::shared_ptr<rtc::WebSocket> m_ws;
    QTimer m_heartbeat;
    QTimer m_reconnect;
    QHash<QString, ConnPtr> m_conns;
};

} // namespace MI
