#include "Downloader.h"
#include "Net.h"
#include <QDir>
#include <QFileInfo>
#include <QTimer>

namespace MI {

Downloader::Downloader(QObject *parent) : QObject(parent) {}

Downloader::~Downloader()
{
    abort();
    qDeleteAll(m_queue);
}

bool Downloader::algoFor(const QString &name, QCryptographicHash::Algorithm *out)
{
    const QString n = name.toLower();
    if (n == QLatin1String("sha1")) *out = QCryptographicHash::Sha1;
    else if (n == QLatin1String("sha256")) *out = QCryptographicHash::Sha256;
    else if (n == QLatin1String("sha512")) *out = QCryptographicHash::Sha512;
    else if (n == QLatin1String("md5")) *out = QCryptographicHash::Md5;
    else return false;
    return true;
}

void Downloader::enqueue(const QList<DownloadItem> &items)
{
    for (const auto &it : items) {
        auto *j = new Job;
        j->item = it;
        m_queue.enqueue(j);
        ++m_total;
    }
}

void Downloader::start()
{
    m_running = true;
    m_aborted = false;
    if (m_total == 0) {
        m_running = false;
        QTimer::singleShot(0, this, [this]() { emit allFinished(true); });
        return;
    }
    pump();
}

void Downloader::abort()
{
    m_aborted = true;
    const auto active = m_active;
    for (Job *j : active) {
        if (j->reply) {
            j->reply->disconnect(this);
            j->reply->abort();
            j->reply->deleteLater();
            j->reply = nullptr;
        }
        cleanup(j);
        m_active.removeAll(j);
        delete j;
    }
    qDeleteAll(m_queue);
    m_queue.clear();
    m_running = false;
}

void Downloader::pump()
{
    while (!m_aborted && m_active.size() < m_concurrency && !m_queue.isEmpty()) {
        Job *j = m_queue.dequeue();
        m_active.append(j);
        startJob(j);
    }
}

void Downloader::cleanup(Job *job)
{
    if (job->file) {
        job->file->close();
        delete job->file;
        job->file = nullptr;
    }
    delete job->sha256;
    job->sha256 = nullptr;
    delete job->check;
    job->check = nullptr;
}

void Downloader::startJob(Job *job)
{
    ++job->attempts;
    job->size = 0;
    cleanup(job);

    QDir().mkpath(QFileInfo(job->item.destPath).absolutePath());
    job->file = new QFile(job->item.destPath + QStringLiteral(".part"));
    if (!job->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishJob(job, false, tr("無法寫入檔案：%1").arg(job->file->fileName()));
        return;
    }
    job->sha256 = new QCryptographicHash(QCryptographicHash::Sha256);
    QCryptographicHash::Algorithm algo;
    if (!job->item.expectedHash.isEmpty() && algoFor(job->item.expectedAlgo, &algo))
        job->check = new QCryptographicHash(algo);

    QNetworkReply *reply = Net::i().nam()->get(Net::makeRequest(job->item.url));
    job->reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, job, reply]() {
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty() || !job->file)
            return;
        job->file->write(chunk);
        job->sha256->addData(chunk);
        if (job->check)
            job->check->addData(chunk);
        job->size += chunk.size();
        m_received += chunk.size();
        emit progress(m_finished, m_total, m_received);
    });
    connect(reply, &QNetworkReply::finished, this, [this, job, reply]() {
        if (m_aborted)
            return;
        job->reply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString err = code ? QStringLiteral("HTTP %1").arg(code) : reply->errorString();
            if (job->attempts < 3 && (code == 0 || code >= 500 || code == 429)) {
                m_received -= job->size;
                startJob(job);
                return;
            }
            finishJob(job, false, err);
            return;
        }
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty()) {
            job->file->write(rest);
            job->sha256->addData(rest);
            if (job->check)
                job->check->addData(rest);
            job->size += rest.size();
        }
        const QString got256 = QString::fromLatin1(job->sha256->result().toHex());
        if (job->check) {
            const QString got = QString::fromLatin1(job->check->result().toHex());
            if (got.compare(job->item.expectedHash, Qt::CaseInsensitive) != 0) {
                if (job->attempts < 2) {
                    m_received -= job->size;
                    startJob(job);
                    return;
                }
                finishJob(job, false, tr("雜湊驗證失敗（預期 %1）").arg(job->item.expectedHash));
                return;
            }
        }
        job->file->flush();
        job->file->close();
        const QString part = job->file->fileName();
        QFile::remove(job->item.destPath);
        if (!QFile::rename(part, job->item.destPath)) {
            finishJob(job, false, tr("無法寫入檔案：%1").arg(job->item.destPath));
            return;
        }
        DownloadResult r;
        r.id = job->item.id;
        r.ok = true;
        r.sha256 = got256;
        r.size = job->size;
        cleanup(job);
        m_active.removeAll(job);
        delete job;
        ++m_finished;
        emit itemFinished(r);
        emit progress(m_finished, m_total, m_received);
        if (m_finished == m_total) {
            m_running = false;
            emit allFinished(m_allOk);
        } else {
            pump();
        }
    });
}

void Downloader::finishJob(Job *job, bool ok, const QString &err)
{
    Q_UNUSED(ok)
    DownloadResult r;
    r.id = job->item.id;
    r.ok = false;
    r.error = err;
    const QString part = job->file ? job->file->fileName() : QString();
    cleanup(job);
    if (!part.isEmpty())
        QFile::remove(part);
    m_active.removeAll(job);
    delete job;
    m_allOk = false;
    ++m_finished;
    emit itemFinished(r);
    emit progress(m_finished, m_total, m_received);
    if (m_finished == m_total) {
        m_running = false;
        emit allFinished(false);
    } else {
        pump();
    }
}

} // namespace MI
