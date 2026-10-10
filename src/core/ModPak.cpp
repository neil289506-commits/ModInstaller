#include "ModPak.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

namespace MI {

struct ZipWriter::Impl {
    QString path;
    QuaZip zip;
};

ZipWriter::ZipWriter(const QString &path) : d(new Impl)
{
    d->path = path;
    d->zip.setZipName(path);
}
ZipWriter::~ZipWriter()
{
    close();
    delete d;
}

bool ZipWriter::open(QString *err)
{
    QDir().mkpath(QFileInfo(d->path).absolutePath());
    d->zip.setZipName(d->path);
    if (!d->zip.open(QuaZip::mdCreate)) {
        if (err)
            *err = QStringLiteral("無法建立檔案：%1").arg(d->path);
        return false;
    }
    return true;
}

bool ZipWriter::addBytes(const QString &entry, const QByteArray &data, bool compress)
{
    QuaZipFile out(&d->zip);
    QuaZipNewInfo info(entry);
    if (!out.open(QIODevice::WriteOnly, info, nullptr, 0, compress ? 8 : 0))
        return false;
    out.write(data);
    out.close();
    return out.getZipError() == 0;
}

bool ZipWriter::addFile(const QString &entry, const QString &diskPath, bool compress)
{
    QFile in(diskPath);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    QuaZipFile out(&d->zip);
    QuaZipNewInfo info(entry, diskPath);
    if (!out.open(QIODevice::WriteOnly, info, nullptr, 0, compress ? 8 : 0))
        return false;
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(1 << 20);
        if (out.write(chunk) != chunk.size()) {
            out.close();
            return false;
        }
    }
    out.close();
    return out.getZipError() == 0;
}

bool ZipWriter::close()
{
    if (d->zip.isOpen()) {
        d->zip.close();
        return d->zip.getZipError() == 0;
    }
    return true;
}

QList<InstalledMod> PakData::asInstalled() const
{
    QList<InstalledMod> l;
    for (const auto &m : mods) {
        InstalledMod i;
        i.provider = m.provider;
        i.projectId = m.projectId;
        i.versionId = m.versionId;
        i.fileName = m.fileName;
        l << i;
    }
    return l;
}

bool ModPak::write(const QString &path, const PakData &data, QString *err)
{
    QJsonObject version;
    version["formatVersion"] = 1;
    version["name"] = data.name;
    version["minecraft"] = data.target.mcVersion;
    version["loader"] = loaderId(data.target.loader);
    version["loaderVersion"] = data.target.loaderVersion;
    version["createdBy"] = QStringLiteral("ModInstaller");
    QJsonArray platforms;
    QJsonArray mods;
    for (const auto &m : data.mods) {
        QJsonObject p;
        p["projectId"] = m.projectId;
        p["name"] = m.name;
        p["platform"] = providerId(m.provider);
        p["website"] = m.websiteUrl;
        platforms << p;

        QJsonObject o;
        o["projectId"] = m.projectId;
        o["platform"] = providerId(m.provider);
        o["name"] = m.name;
        o["fileName"] = m.fileName;
        o["versionId"] = m.versionId;
        o["version"] = m.versionNumber;
        o["url"] = m.url;
        o["sha256"] = m.sha256;
        mods << o;
    }
    version["mods"] = platforms;
    QJsonObject info;
    info["formatVersion"] = 1;
    info["mods"] = mods;

    // 先寫到暫存檔再取代，避免寫到一半損毀既有檔案
    const QString tmp = path + QStringLiteral(".tmp");
    QFile::remove(tmp);
    {
        ZipWriter zw(tmp);
        if (!zw.open(err))
            return false;
        const bool ok = zw.addBytes(QStringLiteral("version.info"), QJsonDocument(version).toJson(QJsonDocument::Indented)) &&
                        zw.addBytes(QStringLiteral("mod.info"), QJsonDocument(info).toJson(QJsonDocument::Indented));
        if (!zw.close() || !ok) {
            QFile::remove(tmp);
            if (err)
                *err = QStringLiteral("寫入 modpak 失敗");
            return false;
        }
    }
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        if (err)
            *err = QStringLiteral("無法寫入檔案：%1").arg(path);
        return false;
    }
    return true;
}

bool ModPak::read(const QString &path, PakData *out, QString *err)
{
    QuaZip zip(path);
    if (!zip.open(QuaZip::mdUnzip)) {
        if (err)
            *err = QStringLiteral("無法開啟 modpak：%1").arg(path);
        return false;
    }
    auto readEntry = [&](const QString &name, QByteArray *data) {
        if (!zip.setCurrentFile(name))
            return false;
        QuaZipFile f(&zip);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        *data = f.readAll();
        return true;
    };
    QByteArray vb, mb;
    if (!readEntry(QStringLiteral("version.info"), &vb) || !readEntry(QStringLiteral("mod.info"), &mb)) {
        if (err)
            *err = QStringLiteral("modpak 缺少 version.info 或 mod.info");
        return false;
    }
    const QJsonObject v = QJsonDocument::fromJson(vb).object();
    const QJsonObject m = QJsonDocument::fromJson(mb).object();
    if (v.isEmpty() || m.isEmpty()) {
        if (err)
            *err = QStringLiteral("modpak 內容格式錯誤");
        return false;
    }
    PakData d;
    d.name = v.value("name").toString();
    d.target.mcVersion = v.value("minecraft").toString();
    d.target.loaderVersion = v.value("loaderVersion").toString();
    if (!loaderFromId(v.value("loader").toString(), &d.target.loader)) {
        if (err)
            *err = QStringLiteral("不支援的載入器：%1").arg(v.value("loader").toString());
        return false;
    }
    QHash<QString, QJsonObject> platforms;
    for (const auto &p : v.value("mods").toArray()) {
        const QJsonObject po = p.toObject();
        platforms.insert(po.value("platform").toString() + QLatin1Char('/') + po.value("projectId").toString(), po);
    }
    for (const auto &e : m.value("mods").toArray()) {
        const QJsonObject o = e.toObject();
        PakMod pm;
        pm.projectId = o.value("projectId").toString();
        QString plat = o.value("platform").toString();
        if (plat.isEmpty()) {
            for (auto it = platforms.begin(); it != platforms.end(); ++it)
                if (it.key().endsWith(QLatin1Char('/') + pm.projectId))
                    plat = it.value().value("platform").toString();
        }
        pm.provider = providerFromId(plat);
        pm.name = o.value("name").toString();
        pm.fileName = o.value("fileName").toString();
        pm.versionId = o.value("versionId").toString();
        pm.versionNumber = o.value("version").toString();
        pm.url = o.value("url").toString();
        pm.sha256 = o.value("sha256").toString();
        pm.websiteUrl = platforms.value(providerId(pm.provider) + QLatin1Char('/') + pm.projectId).value("website").toString();
        d.mods << pm;
    }
    *out = d;
    return true;
}

} // namespace MI
