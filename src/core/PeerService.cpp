// SPDX-License-Identifier: GPL-3.0-only
#include "PeerService.h"
#include "Net.h"
#include "Settings.h"
#include "Util.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QStandardPaths>
#include <algorithm>
#include <rtc/rtc.hpp>

namespace MI {

namespace {
const QString kPeerPrefix = QStringLiteral("modinstaller-");
constexpr qint64 kChunk = 16 * 1024;
constexpr uint64_t kHighWater = 1024 * 1024;
constexpr uint64_t kLowWater = 256 * 1024;
constexpr int kRequestTimeoutMs = 120000;
constexpr qint64 kMaxFileSize = qint64(8) * 1024 * 1024 * 1024;
constexpr int kMaxPending = 5;


QString randomString(int n, const char *alphabet)
{
    QString s;
    const int len = int(qstrlen(alphabet));
    for (int i = 0; i < n; ++i)
        s += QLatin1Char(alphabet[QRandomGenerator::system()->bounded(len)]);
    return s;
}

std::string str(const QString &s) { return s.toStdString(); }

QByteArray compact(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

rtc::Configuration iceConfig()
{
    // 與 PeerJS 瀏覽器端相同的預設 ICE 伺服器
    rtc::Configuration cfg;
    cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");
    cfg.iceServers.emplace_back("turn:peerjs:peerjsp@eu-0.turn.peerjs.com:3478");
    cfg.iceServers.emplace_back("turn:peerjs:peerjsp@us-0.turn.peerjs.com:3478");
    return cfg;
}
} // namespace

struct PeerService::Conn {
    bool sender = false;
    QString id;
    QString peerId;
    QString peerCode;
    QString fileName;
    qint64 size = 0;
    QString path;       // 傳送：來源檔；接收：最終儲存路徑
    QFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    qint64 transferred = 0;
    bool accepted = false;
    bool remoteSet = false;
    bool open = false;
    bool doneSent = false;
    bool finished = false;
    bool metaOk = false;
    QString offerSdp;
    QList<QPair<QString, QString>> pending; // 遠端候選（等待設定遠端描述）
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    QTimer *timer = nullptr;
};

PeerService &PeerService::i()
{
    static PeerService *s = new PeerService; // 程式結束時由作業系統回收
    return *s;
}

PeerService::PeerService(QObject *parent) : QObject(parent)
{
    rtc::InitLogger(rtc::LogLevel::Warning);
    m_heartbeat.setInterval(5000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this]() {
        if (m_ws && m_state == State::Online)
            m_ws->send(std::string("{\"type\":\"HEARTBEAT\"}"));
    });
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this]() {
        if (m_running)
            connectSignaling();
    });
}

PeerService::~PeerService()
{
    closeSignaling();
}

QString PeerService::registryUrl()
{
    QString u = qEnvironmentVariable("MODINSTALLER_REGISTRY_URL");
    if (u.isEmpty())
        u = Settings::i().registryUrl();
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    return u.trimmed();
}

QString PeerService::peerBaseUrl()
{
    const QString env = qEnvironmentVariable("MODINSTALLER_PEER_URL");
    return env.isEmpty() ? QStringLiteral("wss://0.peerjs.com:443/peerjs") : env;
}

QString PeerService::peerIdFor(const QString &code) { return kPeerPrefix + code; }

QString PeerService::codeFromPeerId(const QString &peerId)
{
    return peerId.startsWith(kPeerPrefix) ? peerId.mid(kPeerPrefix.size()) : QString();
}

QString PeerService::stateText() const
{
    switch (m_state) {
    case State::Idle: return QCoreApplication::translate("PeerService", "已停止");
    case State::Registering: return QCoreApplication::translate("PeerService", "註冊代碼中…");
    case State::Connecting: return QCoreApplication::translate("PeerService", "連線中…");
    case State::Online: return QCoreApplication::translate("PeerService", "已連線");
    case State::Offline: return QCoreApplication::translate("PeerService", "離線（重試中）");
    case State::Error: return m_stateDetail.isEmpty() ? QCoreApplication::translate("PeerService", "發生錯誤") : m_stateDetail;
    }
    return {};
}

void PeerService::setState(State s, const QString &detail)
{
    m_state = s;
    m_stateDetail = detail;
    emit stateChanged();
}

// ---------------------------------------------------------------- 代碼與註冊

void PeerService::start()
{
    if (m_running)
        return;
    m_running = true;
    m_rec = DeviceCode::load();
    if (!DeviceCode::isValid(m_rec.code)) {
        m_rec.code = DeviceCode::generate();
        m_rec.registered = false;
        DeviceCode::save(m_rec);
    }
    emit codeChanged(m_rec.code);
    ensureRegistered();
}

void PeerService::restart()
{
    stop();
    start();
}

void PeerService::stop()
{
    m_running = false;
    m_reconnect.stop();
    const auto ids = m_conns.keys();
    for (const auto &id : ids)
        failConn(id, QStringLiteral("cancelled"));
    closeSignaling();
    setState(State::Idle);
}

void PeerService::ensureRegistered()
{
    if (m_rec.registered || registryUrl().isEmpty()) {
        // 沒有設定註冊服務時，以本機隨機代碼直接運作（碰撞機率極低，但無法保證全域唯一）
        connectSignaling();
        return;
    }
    registerCode(0);
}

void PeerService::registerCode(int attempt)
{
    if (!m_running)
        return;
    setState(State::Registering);
    QJsonObject body;
    body[QStringLiteral("code")] = m_rec.code;
    const QString code = m_rec.code;
    Net::i().postJson(QUrl(registryUrl() + QStringLiteral("/api/register")), {{"Accept", "application/json"}}, compact(body), this,
                      [this, attempt, code](bool ok, const QByteArray &, const QString &err) {
                          if (!m_running || code != m_rec.code)
                              return;
                          if (ok) {
                              m_rec.registered = true;
                              DeviceCode::save(m_rec);
                              connectSignaling();
                              return;
                          }
                          if (err.startsWith(QLatin1String("HTTP 409")) && attempt < 10) {
                              // 代碼已被其他電腦使用 → 產生新的獨一無二代碼再註冊
                              m_rec.code = DeviceCode::generate();
                              m_rec.registered = false;
                              DeviceCode::save(m_rec);
                              emit codeChanged(m_rec.code);
                              registerCode(attempt + 1);
                              return;
                          }
                          setState(State::Error, QCoreApplication::translate("PeerService", "無法向雲端註冊代碼：%1").arg(err));
                          QTimer::singleShot(15000, this, [this]() {
                              if (m_running && !m_rec.registered)
                                  registerCode(0);
                          });
                      });
}

void PeerService::checkCodeExists(const QString &code, QObject *ctx, ExistsCb cb)
{
    if (registryUrl().isEmpty()) {
        cb(false, true);
        return;
    }
    Net::i().getBytes(QUrl(registryUrl() + QStringLiteral("/api/codes/") + code), {{"Accept", "application/json"}}, ctx,
                      [cb](bool ok, const QByteArray &, const QString &err) {
                          if (ok)
                              cb(true, true);
                          else if (err.startsWith(QLatin1String("HTTP 404")))
                              cb(true, false);
                          else
                              cb(false, true); // 查不到狀態 → 不阻擋，直接嘗試傳送
                      },
                      1);
}

// ---------------------------------------------------------------- 訊號伺服器（PeerJS）

QString PeerService::writeCaFile()
{
    // WebSocket 的 TLS 驗證需要 CA 憑證：優先匯出系統憑證，沒有時用內建的 Mozilla 憑證包
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/cacert.pem");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QByteArray pem;
    for (const auto &c : QSslConfiguration::systemCaCertificates())
        pem += c.toPem();
    if (pem.isEmpty()) {
        QFile bundled(QStringLiteral(":/cacert.pem"));
        if (bundled.open(QIODevice::ReadOnly))
            pem = bundled.readAll();
    }
    QFile f(path);
    if (!pem.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(pem);
    return pem.isEmpty() ? QString() : path;
}

void PeerService::closeSignaling()
{
    ++m_generation;
    m_heartbeat.stop();
    if (m_ws) {
        auto ws = m_ws;
        m_ws.reset();
        try {
            ws->close();
        } catch (...) {
        }
    }
}

void PeerService::connectSignaling()
{
    if (!m_running)
        return;
    closeSignaling();
    setState(State::Connecting);

    const QString base = peerBaseUrl();
    const QString token = randomString(10, "abcdefghijklmnopqrstuvwxyz0123456789");
    const QString url = QStringLiteral("%1?key=peerjs&id=%2&token=%3").arg(base, peerIdFor(m_rec.code), token);

    rtc::WebSocketConfiguration wc;
    if (base.startsWith(QLatin1String("wss"))) {
        const QString ca = writeCaFile();
        if (ca.isEmpty()) {
            setState(State::Error, QCoreApplication::translate("PeerService", "找不到可用的 CA 憑證，無法安全連線"));
            return;
        }
        wc.caCertificatePemFile = str(ca);
    }
    wc.connectionTimeout = std::chrono::seconds(15);
    auto ws = std::make_shared<rtc::WebSocket>(wc);
    m_ws = ws;
    const int gen = m_generation;
    auto post = [this, gen](std::function<void()> f) {
        QMetaObject::invokeMethod(this, [this, gen, f]() { if (gen == m_generation) f(); }, Qt::QueuedConnection);
    };
    ws->onMessage([post, this](rtc::message_variant m) {
        if (std::holds_alternative<std::string>(m)) {
            const QString text = QString::fromStdString(std::get<std::string>(m));
            post([this, text]() { onSignal(text); });
        }
    });
    ws->onClosed([post, this]() {
        post([this]() {
            if (!m_running)
                return;
            if (m_state != State::Error)
                setState(State::Offline);
            scheduleReconnect();
        });
    });
    ws->onError([post, this](std::string e) {
        const QString msg = QString::fromStdString(e);
        post([this, msg]() {
            if (!m_running)
                return;
            setState(State::Offline, msg);
            scheduleReconnect();
        });
    });
    try {
        ws->open(str(url));
    } catch (const std::exception &e) {
        setState(State::Error, QString::fromUtf8(e.what()));
        scheduleReconnect();
    }
}

void PeerService::scheduleReconnect()
{
    m_heartbeat.stop();
    ++m_reconnectAttempt;
    const int ms = qMin(30000, 3000 * m_reconnectAttempt);
    m_reconnect.start(ms);
}

void PeerService::sendSignal(const QString &type, const QString &dstPeerId, const QJsonObject &payload)
{
    if (!m_ws)
        return;
    QJsonObject o;
    o[QStringLiteral("type")] = type;
    o[QStringLiteral("dst")] = dstPeerId;
    o[QStringLiteral("payload")] = payload;
    try {
        m_ws->send(compact(o).toStdString());
    } catch (...) {
    }
}

void PeerService::onSignal(const QString &text)
{
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString type = o.value(QStringLiteral("type")).toString();
    const QString src = o.value(QStringLiteral("src")).toString();
    const QJsonObject payload = o.value(QStringLiteral("payload")).toObject();
    if (type == QLatin1String("OPEN")) {
        m_reconnectAttempt = 0;
        setState(State::Online);
        m_heartbeat.start();
    } else if (type == QLatin1String("ID-TAKEN")) {
        setState(State::Error, QCoreApplication::translate("PeerService", "此代碼正在另一個視窗或裝置上線中"));
        closeSignaling();
        m_reconnect.start(10000);
    } else if (type == QLatin1String("INVALID-KEY") || type == QLatin1String("ERROR")) {
        setState(State::Error, payload.value(QStringLiteral("msg")).toString(QCoreApplication::translate("PeerService", "訊號伺服器錯誤")));
    } else if (type == QLatin1String("OFFER")) {
        handleOffer(src, payload);
    } else if (type == QLatin1String("ANSWER")) {
        handleAnswer(src, payload);
    } else if (type == QLatin1String("CANDIDATE")) {
        handleCandidate(src, payload);
    } else if (type == QLatin1String("LEAVE")) {
        handleLeave(src, payload);
    } else if (type == QLatin1String("EXPIRE")) {
        handleExpire(src);
    }
}

// ---------------------------------------------------------------- 連線物件

PeerService::ConnPtr PeerService::makeConn(const QString &id, bool sender)
{
    auto c = std::make_shared<Conn>();
    c->id = id;
    c->sender = sender;
    c->timer = new QTimer(this);
    c->timer->setSingleShot(true);
    m_conns.insert(id, c);
    return c;
}

void PeerService::removeConn(const QString &id)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    ConnPtr c = it.value();
    m_conns.erase(it);
    if (c->timer) {
        c->timer->stop();
        c->timer->deleteLater();
        c->timer = nullptr;
    }
    if (c->file.isOpen())
        c->file.close();
    // 在主執行緒關閉，並延後釋放，避免在 libdatachannel 的回呼中銷毀自己
    QTimer::singleShot(1500, this, [c]() {
        try {
            if (c->dc)
                c->dc->close();
            if (c->pc)
                c->pc->close();
        } catch (...) {
        }
    });
}

void PeerService::setupPeerConnection(const ConnPtr &c)
{
    const QString id = c->id;
    const QString dst = c->peerId;
    auto pc = std::make_shared<rtc::PeerConnection>(iceConfig());
    c->pc = pc;
    auto post = [this](std::function<void()> f) { QMetaObject::invokeMethod(this, f, Qt::QueuedConnection); };

    pc->onLocalDescription([this, post, id, dst, sender = c->sender, fileName = c->fileName, size = c->size](rtc::Description d) {
        const QString type = QString::fromStdString(d.typeString());
        const QString sdp = QString::fromStdString(std::string(d));
        post([=]() {
            auto it = m_conns.find(id);
            if (it == m_conns.end())
                return;
            QJsonObject sdpObj;
            sdpObj[QStringLiteral("type")] = type;
            sdpObj[QStringLiteral("sdp")] = sdp;
            QJsonObject p;
            p[QStringLiteral("sdp")] = sdpObj;
            p[QStringLiteral("type")] = QStringLiteral("data");
            p[QStringLiteral("connectionId")] = id;
            p[QStringLiteral("browser")] = QStringLiteral("native");
            if (sender) {
                p[QStringLiteral("label")] = id;
                p[QStringLiteral("reliable")] = true;
                p[QStringLiteral("serialization")] = QStringLiteral("none");
                QJsonObject meta;
                meta[QStringLiteral("app")] = QStringLiteral("ModInstaller");
                meta[QStringLiteral("kind")] = QStringLiteral("transfer-request");
                meta[QStringLiteral("fileName")] = fileName;
                meta[QStringLiteral("size")] = double(size);
                p[QStringLiteral("metadata")] = meta;
                sendSignal(QStringLiteral("OFFER"), dst, p);
            } else {
                sendSignal(QStringLiteral("ANSWER"), dst, p);
            }
        });
    });
    pc->onLocalCandidate([this, post, id, dst](rtc::Candidate cand) {
        const QString candidate = QString::fromStdString(cand.candidate());
        const QString mid = QString::fromStdString(cand.mid());
        post([=]() {
            if (!m_conns.contains(id))
                return;
            QJsonObject cobj;
            cobj[QStringLiteral("candidate")] = candidate;
            cobj[QStringLiteral("sdpMid")] = mid;
            cobj[QStringLiteral("sdpMLineIndex")] = 0;
            QJsonObject p;
            p[QStringLiteral("candidate")] = cobj;
            p[QStringLiteral("type")] = QStringLiteral("data");
            p[QStringLiteral("connectionId")] = id;
            sendSignal(QStringLiteral("CANDIDATE"), dst, p);
        });
    });
    pc->onStateChange([this, post, id](rtc::PeerConnection::State s) {
        if (s == rtc::PeerConnection::State::Failed)
            post([=]() { onConnLost(id, QCoreApplication::translate("PeerService", "連線失敗（可能被防火牆或網路環境阻擋）")); });
    });
}

void PeerService::onConnLost(const QString &id, const QString &why)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end() || it.value()->finished)
        return;
    failConn(id, QStringLiteral("failed:") + why);
}

void PeerService::failConn(const QString &id, const QString &reason)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    ConnPtr c = it.value();
    if (c->finished)
        return;
    c->finished = true;
    if (c->sender) {
        emit sendFinished(id, false, reason);
    } else {
        if (c->accepted) {
            const QString part = c->file.fileName();
            if (c->file.isOpen())
                c->file.close();
            if (!part.isEmpty())
                QFile::remove(part);
            emit recvFinished(id, false, reason);
        } else {
            emit requestExpired(id);
        }
    }
    removeConn(id);
}

void PeerService::finishConn(const QString &id)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    it.value()->finished = true;
    removeConn(id);
}

// ---------------------------------------------------------------- 傳送端

QString PeerService::sendFile(const QString &peerCodeIn, const QString &path, QString *error)
{
    const QString peerCode = DeviceCode::normalize(peerCodeIn);
    auto fail = [&](const QString &m) {
        if (error)
            *error = m;
        return QString();
    };
    if (!DeviceCode::isValid(peerCode))
        return fail(QCoreApplication::translate("PeerService", "代碼必須是 12 碼英數字"));
    if (peerCode == m_rec.code)
        return fail(QCoreApplication::translate("PeerService", "不能傳給自己"));
    if (m_state != State::Online)
        return fail(QCoreApplication::translate("PeerService", "尚未連線到伺服器（%1）").arg(stateText()));
    QFileInfo fi(path);
    if (!fi.isFile() || fi.size() <= 0)
        return fail(QCoreApplication::translate("PeerService", "找不到檔案或檔案是空的"));
    if (fi.size() > kMaxFileSize)
        return fail(QCoreApplication::translate("PeerService", "檔案太大（上限 8 GB）"));
    const QString suffix = fi.suffix().toLower();
    if (suffix != QLatin1String("modpak") && suffix != QLatin1String("zip"))
        return fail(QCoreApplication::translate("PeerService", "只能傳送 .modpak 或 .zip 檔案"));

    const QString id = QStringLiteral("dc_") + randomString(12, "abcdefghijklmnopqrstuvwxyz0123456789");
    ConnPtr c = makeConn(id, true);
    c->peerCode = peerCode;
    c->peerId = peerIdFor(peerCode);
    c->fileName = fi.fileName();
    c->size = fi.size();
    c->path = path;
    c->file.setFileName(path);
    if (!c->file.open(QIODevice::ReadOnly)) {
        removeConn(id);
        return fail(QCoreApplication::translate("PeerService", "無法讀取檔案"));
    }
    setupPeerConnection(c);
    connect(c->timer, &QTimer::timeout, this, [this, id]() { failConn(id, QStringLiteral("timeout")); });
    c->timer->start(kRequestTimeoutMs);

    auto post = [this](std::function<void()> f) { QMetaObject::invokeMethod(this, f, Qt::QueuedConnection); };
    try {
        auto dc = c->pc->createDataChannel(str(id)); // 會自動產生 OFFER
        c->dc = dc;
        dc->setBufferedAmountLowThreshold(kLowWater);
        dc->onOpen([post, this, id]() { post([=]() { onDataOpen(id); }); });
        dc->onBufferedAmountLow([post, this, id]() { post([=]() { pump(id); }); });
        dc->onClosed([post, this, id]() {
            post([=]() {
                auto it = m_conns.find(id);
                if (it != m_conns.end() && !it.value()->finished)
                    failConn(id, QStringLiteral("failed:") + QCoreApplication::translate("PeerService", "連線已中斷"));
            });
        });
        dc->onMessage([](rtc::binary) {},
                      [post, this, id](std::string s) {
                          const QString t = QString::fromStdString(s);
                          post([=]() { onDataText(id, t); });
                      });
    } catch (const std::exception &e) {
        failConn(id, QStringLiteral("failed:") + QString::fromUtf8(e.what()));
        return fail(QString::fromUtf8(e.what()));
    }
    emit sendStatus(id, QCoreApplication::translate("PeerService", "等待對方接受…"));
    return id;
}

void PeerService::handleAnswer(const QString &srcPeerId, const QJsonObject &payload)
{
    const QString id = payload.value(QStringLiteral("connectionId")).toString();
    auto it = m_conns.find(id);
    if (it == m_conns.end() || !it.value()->sender || it.value()->peerId != srcPeerId)
        return;
    ConnPtr c = it.value();
    const QString sdp = payload.value(QStringLiteral("sdp")).toObject().value(QStringLiteral("sdp")).toString();
    if (sdp.isEmpty() || c->remoteSet)
        return;
    try {
        c->pc->setRemoteDescription(rtc::Description(str(sdp), "answer"));
        c->remoteSet = true;
        for (const auto &cand : c->pending)
            c->pc->addRemoteCandidate(rtc::Candidate(str(cand.first), str(cand.second)));
        c->pending.clear();
    } catch (const std::exception &e) {
        failConn(id, QStringLiteral("failed:") + QString::fromUtf8(e.what()));
        return;
    }
    emit sendStatus(id, QCoreApplication::translate("PeerService", "對方已接受，正在建立連線…"));
    c->timer->start(60000); // 接受後最多等 60 秒建立連線
}

void PeerService::onDataOpen(const QString &id)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    ConnPtr c = it.value();
    c->open = true;
    if (!c->sender)
        return;
    c->timer->stop();
    QJsonObject meta;
    meta[QStringLiteral("t")] = QStringLiteral("meta");
    meta[QStringLiteral("name")] = c->fileName;
    meta[QStringLiteral("size")] = double(c->size);
    try {
        c->dc->send(compact(meta).toStdString());
    } catch (...) {
    }
    emit sendStatus(id, QCoreApplication::translate("PeerService", "傳送中…"));
    pump(id);
}

void PeerService::pump(const QString &id)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    ConnPtr c = it.value();
    if (!c->sender || !c->open || c->finished || c->doneSent || !c->dc)
        return;
    try {
        while (c->dc->bufferedAmount() < kHighWater && !c->file.atEnd()) {
            const QByteArray chunk = c->file.read(kChunk);
            if (chunk.isEmpty())
                break;
            c->hash.addData(chunk);
            c->dc->send(reinterpret_cast<const std::byte *>(chunk.constData()), size_t(chunk.size()));
            c->transferred += chunk.size();
        }
        emit sendProgress(id, c->transferred, c->size);
        if (c->file.atEnd() && c->transferred >= c->size) {
            QJsonObject done;
            done[QStringLiteral("t")] = QStringLiteral("done");
            done[QStringLiteral("sha256")] = QString::fromLatin1(c->hash.result().toHex());
            c->dc->send(compact(done).toStdString());
            c->doneSent = true;
            c->timer->start(60000); // 等待對方確認
        }
    } catch (const std::exception &e) {
        failConn(id, QStringLiteral("failed:") + QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------- 接收端

void PeerService::handleOffer(const QString &srcPeerId, const QJsonObject &payload)
{
    const QString fromCode = codeFromPeerId(srcPeerId);
    const QString id = payload.value(QStringLiteral("connectionId")).toString();
    const QJsonObject meta = payload.value(QStringLiteral("metadata")).toObject();
    const QString sdp = payload.value(QStringLiteral("sdp")).toObject().value(QStringLiteral("sdp")).toString();
    if (!DeviceCode::isValid(fromCode) || id.isEmpty() || sdp.isEmpty() || m_conns.contains(id))
        return;
    auto leave = [&](const QString &reason) {
        QJsonObject p;
        p[QStringLiteral("connectionId")] = id;
        p[QStringLiteral("reason")] = reason;
        sendSignal(QStringLiteral("LEAVE"), srcPeerId, p);
    };
    if (meta.value(QStringLiteral("app")).toString() != QLatin1String("ModInstaller") ||
        meta.value(QStringLiteral("kind")).toString() != QLatin1String("transfer-request")) {
        leave(QStringLiteral("unsupported"));
        return;
    }
    // 檔名只取最後一段並清理，避免路徑穿越
    QString name = sanitizeFileName(QFileInfo(meta.value(QStringLiteral("fileName")).toString()).fileName());
    const qint64 size = qint64(meta.value(QStringLiteral("size")).toDouble());
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (name.isEmpty() || size <= 0 || size > kMaxFileSize || (suffix != QLatin1String("modpak") && suffix != QLatin1String("zip"))) {
        leave(QStringLiteral("unsupported"));
        return;
    }
    int pendingCount = 0;
    for (const auto &c : std::as_const(m_conns))
        if (!c->sender && !c->accepted)
            ++pendingCount;
    if (pendingCount >= kMaxPending) {
        leave(QStringLiteral("busy"));
        return;
    }
    ConnPtr c = makeConn(id, false);
    c->peerId = srcPeerId;
    c->peerCode = fromCode;
    c->fileName = name;
    c->size = size;
    c->offerSdp = sdp;
    connect(c->timer, &QTimer::timeout, this, [this, id, leave]() {
        auto it = m_conns.find(id);
        if (it == m_conns.end())
            return;
        QJsonObject p;
        p[QStringLiteral("connectionId")] = id;
        p[QStringLiteral("reason")] = QStringLiteral("timeout");
        sendSignal(QStringLiteral("LEAVE"), it.value()->peerId, p);
        failConn(id, QStringLiteral("timeout"));
    });
    c->timer->start(kRequestTimeoutMs);
    IncomingRequest r;
    r.id = id;
    r.fromCode = fromCode;
    r.fileName = name;
    r.size = size;
    emit incomingRequest(r);
}

void PeerService::reject(const QString &requestId)
{
    auto it = m_conns.find(requestId);
    if (it == m_conns.end() || it.value()->sender || it.value()->accepted)
        return;
    QJsonObject p;
    p[QStringLiteral("connectionId")] = requestId;
    p[QStringLiteral("reason")] = QStringLiteral("rejected");
    sendSignal(QStringLiteral("LEAVE"), it.value()->peerId, p);
    it.value()->finished = true;
    removeConn(requestId);
}

void PeerService::accept(const QString &requestId, const QString &savePath)
{
    auto it = m_conns.find(requestId);
    if (it == m_conns.end() || it.value()->sender || it.value()->accepted)
        return;
    ConnPtr c = it.value();
    c->path = savePath;
    c->file.setFileName(savePath + QStringLiteral(".part"));
    QDir().mkpath(QFileInfo(savePath).absolutePath());
    if (!c->file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit recvFinished(requestId, false, QCoreApplication::translate("PeerService", "無法寫入檔案：%1").arg(savePath));
        reject(requestId);
        return;
    }
    c->accepted = true;
    c->timer->start(90000); // 接受後 90 秒內要建立連線
    setupPeerConnection(c);
    auto post = [this](std::function<void()> f) { QMetaObject::invokeMethod(this, f, Qt::QueuedConnection); };
    const QString id = requestId;
    c->pc->onDataChannel([post, this, id](std::shared_ptr<rtc::DataChannel> dc) {
        dc->onOpen([post, this, id]() { post([=]() { onDataOpen(id); }); });
        dc->onClosed([post, this, id]() {
            post([=]() {
                auto it = m_conns.find(id);
                if (it != m_conns.end() && !it.value()->finished)
                    failConn(id, QCoreApplication::translate("PeerService", "對方中斷了傳輸"));
            });
        });
        dc->onMessage(
            [post, this, id](rtc::binary b) {
                QByteArray data(reinterpret_cast<const char *>(b.data()), qsizetype(b.size()));
                post([=]() { onDataBinary(id, data); });
            },
            [post, this, id](std::string s) {
                const QString t = QString::fromStdString(s);
                post([=]() { onDataText(id, t); });
            });
        post([=]() {
            auto it = m_conns.find(id);
            if (it != m_conns.end())
                it.value()->dc = dc;
        });
    });
    try {
        c->pc->setRemoteDescription(rtc::Description(str(c->offerSdp), "offer"));
        c->remoteSet = true;
        for (const auto &cand : c->pending)
            c->pc->addRemoteCandidate(rtc::Candidate(str(cand.first), str(cand.second)));
        c->pending.clear();
    } catch (const std::exception &e) {
        failConn(id, QStringLiteral("failed:") + QString::fromUtf8(e.what()));
    }
}

void PeerService::onDataBinary(const QString &id, const QByteArray &data)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    ConnPtr c = it.value();
    if (c->sender || c->finished || !c->metaOk)
        return; // 沒收到 meta 之前的資料一律忽略
    c->transferred += data.size();
    if (c->transferred > c->size) {
        failConn(id, QCoreApplication::translate("PeerService", "收到的資料超過宣告的大小"));
        return;
    }
    if (c->file.write(data) != data.size()) {
        failConn(id, QCoreApplication::translate("PeerService", "寫入檔案失敗（磁碟空間不足？）"));
        return;
    }
    c->hash.addData(data);
    emit recvProgress(id, c->transferred, c->size);
}

void PeerService::onDataText(const QString &id, const QString &text)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    ConnPtr c = it.value();
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString t = o.value(QStringLiteral("t")).toString();

    if (c->sender) {
        if (t == QLatin1String("result")) {
            c->finished = true;
            const bool ok = o.value(QStringLiteral("ok")).toBool();
            emit sendFinished(id, ok, ok ? QStringLiteral("ok") : QStringLiteral("failed:") + o.value(QStringLiteral("msg")).toString());
            removeConn(id);
        }
        return;
    }
    // 接收端
    if (t == QLatin1String("meta")) {
        const qint64 size = qint64(o.value(QStringLiteral("size")).toDouble());
        if (QFileInfo(o.value(QStringLiteral("name")).toString()).fileName() != QFileInfo(c->fileName).fileName() || size != c->size) {
            failConn(id, QCoreApplication::translate("PeerService", "傳輸內容與請求不一致"));
            return;
        }
        c->metaOk = true;
        c->timer->stop();
    } else if (t == QLatin1String("done")) {
        const QString sha = QString::fromLatin1(c->hash.result().toHex());
        const bool ok = c->metaOk && c->transferred == c->size && sha.compare(o.value(QStringLiteral("sha256")).toString(), Qt::CaseInsensitive) == 0;
        QJsonObject res;
        res[QStringLiteral("t")] = QStringLiteral("result");
        res[QStringLiteral("ok")] = ok;
        if (!ok)
            res[QStringLiteral("msg")] = QStringLiteral("hash mismatch");
        try {
            if (c->dc)
                c->dc->send(compact(res).toStdString());
        } catch (...) {
        }
        if (!ok) {
            failConn(id, QCoreApplication::translate("PeerService", "檔案驗證失敗（雜湊不符）"));
            return;
        }
        c->file.flush();
        c->file.close();
        QFile::remove(c->path);
        if (!QFile::rename(c->path + QStringLiteral(".part"), c->path)) {
            c->finished = true;
            emit recvFinished(id, false, QCoreApplication::translate("PeerService", "無法寫入檔案：%1").arg(c->path));
            removeConn(id);
            return;
        }
        c->finished = true;
        emit recvFinished(id, true, c->path);
        removeConn(id);
    }
}

// ---------------------------------------------------------------- 其他訊號

void PeerService::handleCandidate(const QString &srcPeerId, const QJsonObject &payload)
{
    const QString id = payload.value(QStringLiteral("connectionId")).toString();
    auto it = m_conns.find(id);
    if (it == m_conns.end() || it.value()->peerId != srcPeerId)
        return;
    ConnPtr c = it.value();
    const QJsonObject cobj = payload.value(QStringLiteral("candidate")).toObject();
    const QString cand = cobj.value(QStringLiteral("candidate")).toString();
    const QString mid = cobj.value(QStringLiteral("sdpMid")).toString();
    if (cand.isEmpty())
        return;
    if (!c->remoteSet || !c->pc) {
        c->pending.append({cand, mid});
        return;
    }
    try {
        c->pc->addRemoteCandidate(rtc::Candidate(str(cand), str(mid)));
    } catch (...) {
    }
}

void PeerService::handleLeave(const QString &srcPeerId, const QJsonObject &payload)
{
    const QString id = payload.value(QStringLiteral("connectionId")).toString();
    const QString reason = payload.value(QStringLiteral("reason")).toString();
    auto it = m_conns.find(id);
    if (it == m_conns.end() || it.value()->peerId != srcPeerId)
        return;
    if (it.value()->sender) {
        static const QStringList known = {QStringLiteral("rejected"), QStringLiteral("timeout"), QStringLiteral("busy"),
                                          QStringLiteral("unsupported"), QStringLiteral("cancelled")};
        failConn(id, known.contains(reason) ? reason : QStringLiteral("failed:") + QCoreApplication::translate("PeerService", "對方中斷了連線"));
    } else {
        failConn(id, QCoreApplication::translate("PeerService", "對方取消了傳輸"));
    }
}

void PeerService::handleExpire(const QString &srcPeerId)
{
    const auto ids = m_conns.keys();
    for (const auto &id : ids) {
        auto c = m_conns.value(id);
        if (c && c->sender && c->peerId == srcPeerId && !c->remoteSet)
            failConn(id, QStringLiteral("offline"));
    }
}

void PeerService::cancel(const QString &id)
{
    auto it = m_conns.find(id);
    if (it == m_conns.end())
        return;
    QJsonObject p;
    p[QStringLiteral("connectionId")] = id;
    p[QStringLiteral("reason")] = QStringLiteral("cancelled");
    sendSignal(QStringLiteral("LEAVE"), it.value()->peerId, p);
    failConn(id, QStringLiteral("cancelled"));
}

} // namespace MI
