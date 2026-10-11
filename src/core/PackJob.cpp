#include "PackJob.h"
#include <algorithm>
#include "ModApi.h"
#include "Util.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSet>

namespace MI {


JobMod JobMod::fromVersion(const ModPack &pack, const ModVersion &v)
{
    JobMod m;
    m.provider = pack.provider;
    m.projectId = pack.addonId;
    m.name = pack.name;
    m.versionId = v.versionId;
    m.versionNumber = v.label();
    m.fileName = v.fileName;
    m.url = v.downloadUrl;
    m.websiteUrl = pack.websiteUrl;
    if (!v.sha512.isEmpty()) {
        m.expectedHash = v.sha512;
        m.expectedAlgo = QStringLiteral("sha512");
    } else if (!v.sha1.isEmpty()) {
        m.expectedHash = v.sha1;
        m.expectedAlgo = QStringLiteral("sha1");
    } else if (!v.md5.isEmpty()) {
        m.expectedHash = v.md5;
        m.expectedAlgo = QStringLiteral("md5");
    }
    return m;
}

JobMod JobMod::fromPak(const PakMod &p)
{
    JobMod m;
    m.provider = p.provider;
    m.projectId = p.projectId;
    m.name = p.name;
    m.versionId = p.versionId;
    m.versionNumber = p.versionNumber;
    m.fileName = p.fileName;
    m.url = p.url;
    m.websiteUrl = p.websiteUrl;
    m.sha256 = p.sha256;
    m.expectedHash = p.sha256;
    m.expectedAlgo = p.sha256.isEmpty() ? QString() : QStringLiteral("sha256");
    return m;
}

PakMod JobMod::toPak() const
{
    PakMod p;
    p.provider = provider;
    p.projectId = projectId;
    p.name = name;
    p.versionId = versionId;
    p.versionNumber = versionNumber;
    p.fileName = fileName;
    p.url = url;
    p.sha256 = sha256;
    p.websiteUrl = websiteUrl;
    return p;
}

PackJob::PackJob(Mode mode, const PackTarget &target, const QString &name, const QString &outputPath, const QList<JobMod> &mods,
                 QObject *parent)
    : QObject(parent), m_mode(mode), m_target(target), m_name(name), m_output(outputPath), m_mods(mods)
{
}

PackJob::~PackJob()
{
    if (m_dl)
        m_dl->abort();
}

void PackJob::start()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    if (!m_tmp->isValid()) {
        fail(QCoreApplication::translate("PackJob", "無法建立暫存資料夾"));
        return;
    }
    resolveUrls();
}

void PackJob::cancel()
{
    m_cancelled = true;
    if (m_dl)
        m_dl->abort();
    fail(QCoreApplication::translate("PackJob", "已取消"));
}

void PackJob::fail(const QString &err)
{
    if (m_finished)
        return;
    m_finished = true;
    emit finished(false, err);
}

void PackJob::resolveUrls()
{
    emit status(QCoreApplication::translate("PackJob", "正在取得下載網址…"));
    QList<int> need;
    for (int i = 0; i < m_mods.size(); ++i)
        if (m_mods[i].needDownload && m_mods[i].url.isEmpty())
            need << i;
    if (need.isEmpty()) {
        startDownloads();
        return;
    }
    m_pendingUrls = need.size();
    for (int i : need) {
        ModVersion v;
        v.addonId = m_mods[i].projectId;
        v.versionId = m_mods[i].versionId;
        ModApi::resolveDownloadUrl(this, m_mods[i].provider, v, [this, i](bool ok, const QString &url, const QString &) {
            if (m_finished)
                return;
            if (ok)
                m_mods[i].url = url;
            else
                m_blocked << i;
            if (--m_pendingUrls == 0) {
                if (!m_blocked.isEmpty()) {
                    std::sort(m_blocked.begin(), m_blocked.end());
                    emit blockedMods(m_blocked);
                } else {
                    startDownloads();
                }
            }
        });
    }
}

void PackJob::provideBlocked(const QHash<int, QString> &paths)
{
    if (m_finished)
        return;
    for (int i : m_blocked) {
        JobMod &m = m_mods[i];
        const QString p = paths.value(i);
        if (p.isEmpty()) {
            m.skipped = true;
            m.needDownload = false;
            continue;
        }
        // 複製到暫存資料夾並計算雜湊
        const QString dest = m_tmp->path() + QLatin1Char('/') + QString::number(i) + QLatin1Char('_') + sanitizeFileName(m.fileName);
        QFile::remove(dest);
        if (!QFile::copy(p, dest)) {
            m.skipped = true;
            m.needDownload = false;
            m.error = QCoreApplication::translate("PackJob", "無法讀取手動下載的檔案");
            continue;
        }
        QFile f(dest);
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (f.open(QIODevice::ReadOnly))
            h.addData(&f);
        m.sha256 = QString::fromLatin1(h.result().toHex());
        m.localPath = dest;
        m.needDownload = false;
    }
    m_blocked.clear();
    startDownloads();
}

void PackJob::startDownloads()
{
    QList<DownloadItem> items;
    for (int i = 0; i < m_mods.size(); ++i) {
        JobMod &m = m_mods[i];
        if (!m.needDownload || m.skipped)
            continue;
        DownloadItem it;
        it.id = QString::number(i);
        it.url = QUrl(m.url);
        it.destPath = m_tmp->path() + QLatin1Char('/') + QString::number(i) + QLatin1Char('_') + sanitizeFileName(m.fileName);
        it.expectedHash = m.expectedHash;
        it.expectedAlgo = m.expectedAlgo;
        items << it;
    }
    if (items.isEmpty()) {
        writeOutput();
        return;
    }
    emit status(QCoreApplication::translate("PackJob", "正在下載模組…"));
    m_dl = new Downloader(this);
    m_dl->setConcurrency(6);
    QStringList errors;
    connect(m_dl, &Downloader::itemFinished, this, [this, errors](const DownloadResult &r) mutable {
        const int idx = r.id.toInt();
        JobMod &m = m_mods[idx];
        if (r.ok) {
            m.sha256 = r.sha256;
            m.localPath = m_tmp->path() + QLatin1Char('/') + r.id + QLatin1Char('_') + sanitizeFileName(m.fileName);
        } else {
            m.error = r.error;
        }
    });
    connect(m_dl, &Downloader::progress, this, [this, total = int(items.size())](int done, int, qint64) { emit progress(done, total); });
    connect(m_dl, &Downloader::allFinished, this, [this](bool ok) {
        if (m_cancelled || m_finished)
            return;
        if (!ok) {
            QStringList errs;
            for (const auto &m : m_mods)
                if (!m.error.isEmpty() && !m.skipped)
                    errs << QStringLiteral("%1 — %2").arg(m.name, m.error);
            fail(QCoreApplication::translate("PackJob", "下載失敗：\n") + errs.join(QLatin1Char('\n')));
            return;
        }
        writeOutput();
    });
    m_dl->enqueue(items);
    m_dl->start();
}

void PackJob::writeOutput()
{
    if (m_finished)
        return;
    emit status(QCoreApplication::translate("PackJob", "正在寫入輸出檔案…"));
    QString err;
    if (m_mode == Mode::Zip) {
        QFile::remove(m_output);
        ZipWriter zw(m_output);
        if (!zw.open(&err)) {
            fail(err);
            return;
        }
        QSet<QString> used;
        bool ok = true;
        for (const auto &m : m_mods) {
            if (m.skipped || m.localPath.isEmpty())
                continue;
            QString entry = QStringLiteral("mods/") + sanitizeFileName(m.fileName);
            for (int n = 1; used.contains(entry.toLower()); ++n)
                entry = QStringLiteral("mods/%1_%2").arg(n).arg(sanitizeFileName(m.fileName));
            used.insert(entry.toLower());
            ok = ok && zw.addFile(entry, m.localPath, false);
        }
        if (!zw.close() || !ok) {
            fail(QCoreApplication::translate("PackJob", "寫入 zip 失敗"));
            return;
        }
    } else if (m_mode == Mode::Pak) {
        PakData d;
        d.name = m_name;
        d.target = m_target;
        for (const auto &m : m_mods)
            d.mods << m.toPak();
        if (!ModPak::write(m_output, d, &err)) {
            fail(err);
            return;
        }
    }
    m_finished = true;
    emit finished(true, QString());
}

} // namespace MI
