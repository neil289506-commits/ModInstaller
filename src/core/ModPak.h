#pragma once
#include "Models.h"
#include <QByteArray>
#include <QString>

namespace MI {

// .modpak 內的單一模組
struct PakMod {
    Provider provider = Provider::Modrinth;
    QString projectId;
    QString name;
    QString versionId;
    QString versionNumber;
    QString fileName;
    QString url;    // 空字串 = 作者禁止第三方下載，需手動下載
    QString sha256;
    QString websiteUrl;
};

struct PakData {
    QString name;
    PackTarget target;
    QList<PakMod> mods;
    QList<InstalledMod> asInstalled() const;
};

// .modpak 是一個 zip：內含 version.info（版本資訊 + 各模組託管平台）與 mod.info（下載地址、版本號、sha256）
class ModPak
{
public:
    static bool write(const QString &path, const PakData &data, QString *err);
    static bool read(const QString &path, PakData *out, QString *err);
};

// zip 輔助（QuaZip）
class ZipWriter
{
public:
    explicit ZipWriter(const QString &path);
    ~ZipWriter();
    bool open(QString *err);
    bool addBytes(const QString &entry, const QByteArray &data, bool compress = true);
    bool addFile(const QString &entry, const QString &diskPath, bool compress = false);
    bool close();

private:
    struct Impl;
    Impl *d;
};

} // namespace MI
