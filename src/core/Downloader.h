#pragma once
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QUrl>

namespace MI {

struct DownloadItem {
    QString id;
    QUrl url;
    QString destPath;
    QString expectedHash;      // 十六進位，可空
    QString expectedAlgo;      // sha1 / sha256 / sha512 / md5
};

struct DownloadResult {
    QString id;
    bool ok = false;
    QString error;
    QString sha256; // 一律計算
    qint64 size = 0;
};

// 並行下載器：多工、重試、雜湊驗證（行為對應 Prism NetJob + ChecksumValidator）
class Downloader : public QObject
{
    Q_OBJECT
public:
    explicit Downloader(QObject *parent = nullptr);
    ~Downloader() override;

    void setConcurrency(int n) { m_concurrency = qMax(1, n); }
    void enqueue(const QList<DownloadItem> &items);
    void start();
    void abort();
    bool isRunning() const { return m_running; }

signals:
    void itemFinished(const MI::DownloadResult &result);
    void progress(int finishedItems, int totalItems, qint64 receivedBytes);
    void allFinished(bool allOk);

private:
    struct Job {
        DownloadItem item;
        int attempts = 0;
        QNetworkReply *reply = nullptr;
        QFile *file = nullptr;
        QCryptographicHash *sha256 = nullptr;
        QCryptographicHash *check = nullptr;
        qint64 size = 0;
    };

    void pump();
    void startJob(Job *job);
    void finishJob(Job *job, bool ok, const QString &err);
    void cleanup(Job *job);
    static bool algoFor(const QString &name, QCryptographicHash::Algorithm *out);

    QQueue<Job *> m_queue;
    QList<Job *> m_active;
    int m_concurrency = 6;
    int m_total = 0;
    int m_finished = 0;
    bool m_allOk = true;
    bool m_running = false;
    bool m_aborted = false;
    qint64 m_received = 0;
};

} // namespace MI
