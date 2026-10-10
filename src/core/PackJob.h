#pragma once
#include "Downloader.h"
#include "ModPak.h"
#include <QHash>
#include <QObject>
#include <QTemporaryDir>
#include <memory>

namespace MI {

struct JobMod {
    Provider provider = Provider::Modrinth;
    QString projectId, name, versionId, versionNumber, fileName, url, websiteUrl;
    QString expectedHash, expectedAlgo; // 下載驗證
    QString sha256;                     // 已知（不需重新下載）或下載後算出
    bool needDownload = true;
    // 以下為執行結果
    QString localPath;
    bool skipped = false;
    QString error;

    static JobMod fromVersion(const ModPack &pack, const ModVersion &v);
    static JobMod fromPak(const PakMod &m);
    PakMod toPak() const;
};

// 打包流程：解析下載網址 → （處理被禁止下載的模組）→ 下載並驗證 → 輸出 zip / modpak
class PackJob : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Zip, Pak, HashOnly };

    PackJob(Mode mode, const PackTarget &target, const QString &name, const QString &outputPath, const QList<JobMod> &mods,
            QObject *parent = nullptr);
    ~PackJob() override;

    void start();
    void cancel();
    // UI 回覆：index → 使用者手動下載的檔案路徑；未出現在 map 的視為略過
    void provideBlocked(const QHash<int, QString> &paths);
    const QList<JobMod> &mods() const { return m_mods; }

signals:
    void status(const QString &text);
    void progress(int done, int total);
    void blockedMods(const QList<int> &indices);
    void finished(bool ok, const QString &err);

private:
    void resolveUrls();
    void startDownloads();
    void writeOutput();
    void fail(const QString &err);

    Mode m_mode;
    PackTarget m_target;
    QString m_name;
    QString m_output;
    QList<JobMod> m_mods;
    std::unique_ptr<QTemporaryDir> m_tmp;
    Downloader *m_dl = nullptr;
    QList<int> m_blocked;
    int m_pendingUrls = 0;
    bool m_cancelled = false;
    bool m_finished = false;
};

} // namespace MI
