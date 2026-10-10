#include "VersionData.h"
#include "Net.h"
#include "Settings.h"
#include "Util.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>

namespace MI {

VersionData::VersionData(QObject *parent) : QObject(parent) {}

QList<VersionData::Source> VersionData::sources()
{
    return {
        {QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"), QStringLiteral("mojang.json"), true},
        {QStringLiteral("https://meta.fabricmc.net/v2/versions/game"), QStringLiteral("fabric_game.json"), false},
        {QStringLiteral("https://meta.fabricmc.net/v2/versions/loader"), QStringLiteral("fabric_loader.json"), false},
        {QStringLiteral("https://meta.quiltmc.org/v3/versions/game"), QStringLiteral("quilt_game.json"), false},
        {QStringLiteral("https://meta.quiltmc.org/v3/versions/loader"), QStringLiteral("quilt_loader.json"), false},
        {QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml"), QStringLiteral("forge_maven.xml"), false},
        {QStringLiteral("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"), QStringLiteral("forge_promos.json"), false},
        {QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml"), QStringLiteral("neoforge_maven.xml"), false},
        {QStringLiteral("https://optifine.net/downloads"), QStringLiteral("optifine.html"), false},
    };
}

QByteArray VersionData::readLocal(const QString &file)
{
    QFile f(Settings::i().versionDir() + QLatin1Char('/') + file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool VersionData::hasLocal() const
{
    return QFile::exists(Settings::i().versionDir() + QStringLiteral("/mojang.json"));
}

void VersionData::fetchAll()
{
    QDir().mkpath(Settings::i().versionDir());
    fetchNext(0, {});
}

void VersionData::fetchNext(int index, QStringList failures)
{
    const auto src = sources();
    if (index >= src.size()) {
        QString err;
        const bool ok = load(&err);
        if (ok && !failures.isEmpty())
            err = tr("部分來源下載失敗（已使用舊資料）：%1").arg(failures.join(QStringLiteral(", ")));
        emit finished(ok, err);
        return;
    }
    const Source s = src.at(index);
    emit progress(index, src.size(), s.file);
    Net::i().getBytes(QUrl(s.url), {}, this, [=, this](bool ok, const QByteArray &data, const QString &err) {
        QStringList f = failures;
        if (ok && !data.isEmpty()) {
            QFile out(Settings::i().versionDir() + QLatin1Char('/') + s.file);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                out.write(data);
        } else {
            if (s.required && !QFile::exists(Settings::i().versionDir() + QLatin1Char('/') + s.file)) {
                emit finished(false, tr("無法下載 %1：%2").arg(s.file, err));
                return;
            }
            f << s.file;
        }
        fetchNext(index + 1, f);
    });
}

bool VersionData::load(QString *err)
{
    m_vanilla.clear();
    m_vanillaIds.clear();
    m_fabricGames.clear();
    m_quiltGames.clear();
    m_fabricLoaders.clear();
    m_quiltLoaders.clear();
    m_forge.clear();
    m_neo.clear();
    m_optifine.clear();

    const QByteArray mojang = readLocal(QStringLiteral("mojang.json"));
    if (mojang.isEmpty()) {
        if (err)
            *err = tr("找不到版本資料，請先更新版本資料。");
        return false;
    }
    parseMojang(mojang);
    parseMeta(readLocal(QStringLiteral("fabric_game.json")), readLocal(QStringLiteral("fabric_loader.json")), &m_fabricGames, &m_fabricLoaders);
    parseMeta(readLocal(QStringLiteral("quilt_game.json")), readLocal(QStringLiteral("quilt_loader.json")), &m_quiltGames, &m_quiltLoaders);
    parseForge(readLocal(QStringLiteral("forge_maven.xml")), readLocal(QStringLiteral("forge_promos.json")));
    parseNeoForge(readLocal(QStringLiteral("neoforge_maven.xml")));
    parseOptiFine(readLocal(QStringLiteral("optifine.html")));
    if (m_vanilla.isEmpty()) {
        if (err)
            *err = tr("版本資料內容無法解析。");
        return false;
    }
    return true;
}

void VersionData::parseMojang(const QByteArray &d)
{
    const QJsonArray arr = QJsonDocument::fromJson(d).object().value("versions").toArray();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        VanillaVersion vv{o.value("id").toString(), o.value("type").toString()};
        if (vv.type != QLatin1String("release") && vv.type != QLatin1String("snapshot"))
            continue;
        m_vanilla << vv;
        m_vanillaIds.insert(vv.id);
    }
}

void VersionData::parseMeta(const QByteArray &game, const QByteArray &loader, QMap<QString, bool> *games, QList<LoaderVersion> *loaders)
{
    for (const auto &g : QJsonDocument::fromJson(game).array()) {
        const QJsonObject o = g.toObject();
        games->insert(o.value("version").toString(), o.value("stable").toBool(true));
    }
    for (const auto &l : QJsonDocument::fromJson(loader).array()) {
        const QJsonObject o = l.toObject();
        LoaderVersion lv;
        lv.version = o.value("version").toString();
        lv.stable = o.contains("stable") ? o.value("stable").toBool() : !lv.version.contains(QLatin1Char('-'));
        if (!lv.version.isEmpty())
            loaders->append(lv);
    }
}

QStringList VersionData::xmlVersions(const QByteArray &xml)
{
    QStringList out;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("version"))
            out << r.readElementText().trimmed();
    }
    return out;
}

void VersionData::parseForge(const QByteArray &xml, const QByteArray &promos)
{
    QMap<QString, QString> recommended, latest;
    const QJsonObject promo = QJsonDocument::fromJson(promos).object().value("promos").toObject();
    for (auto it = promo.begin(); it != promo.end(); ++it) {
        const QString key = it.key();
        if (key.endsWith(QLatin1String("-recommended")))
            recommended.insert(key.left(key.size() - 12), it.value().toString());
        else if (key.endsWith(QLatin1String("-latest")))
            latest.insert(key.left(key.size() - 7), it.value().toString());
    }
    for (const QString &full : xmlVersions(xml)) {
        const int dash = full.indexOf(QLatin1Char('-'));
        if (dash <= 0)
            continue;
        LoaderVersion lv;
        lv.mc = full.left(dash);
        lv.version = full.mid(dash + 1);
        lv.recommended = recommended.value(lv.mc) == lv.version;
        m_forge[lv.mc].append(lv);
    }
    for (auto it = m_forge.begin(); it != m_forge.end(); ++it) {
        auto &list = it.value();
        std::stable_sort(list.begin(), list.end(), [](const LoaderVersion &a, const LoaderVersion &b) { return semGt(a.version, b.version); });
    }
}

void VersionData::parseNeoForge(const QByteArray &xml)
{
    for (const QString &ver : xmlVersions(xml)) {
        // 20.4.237 → 1.20.4；21.0.x → 1.21；21.1.x → 1.21.1（依 Mojang 版本表驗證）
        const QStringList parts = ver.split(QLatin1Char('.'));
        if (parts.size() < 2 || !parts[0].at(0).isDigit())
            continue;
        const int major = parts[0].toInt();
        const int minor = parts[1].toInt();
        if (major < 20)
            continue;
        QStringList candidates;
        candidates << (minor == 0 ? QStringLiteral("1.%1").arg(major) : QStringLiteral("1.%1.%2").arg(major).arg(minor));
        candidates << QStringLiteral("%1.%2").arg(major).arg(minor);
        QString mc;
        for (const auto &c : candidates)
            if (m_vanillaIds.contains(c)) {
                mc = c;
                break;
            }
        if (mc.isEmpty())
            continue;
        LoaderVersion lv;
        lv.mc = mc;
        lv.version = ver;
        lv.stable = !ver.contains(QLatin1Char('-'));
        m_neo[mc].append(lv);
    }
    for (auto it = m_neo.begin(); it != m_neo.end(); ++it) {
        auto &list = it.value();
        std::stable_sort(list.begin(), list.end(), [](const LoaderVersion &a, const LoaderVersion &b) { return semGt(a.version, b.version); });
        // 標示每個 MC 版本最新的穩定版為推薦
        for (auto &lv : list)
            if (lv.stable) {
                lv.recommended = true;
                break;
            }
    }
}

void VersionData::parseOptiFine(const QByteArray &html)
{
    static const QRegularExpression re(QStringLiteral("OptiFine_(\\d+(?:\\.\\d+)+)_(HD_U_[A-Za-z0-9_]+)\\.jar"));
    auto it = re.globalMatch(QString::fromUtf8(html));
    QSet<QString> seen;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString key = m.captured(1) + QLatin1Char('/') + m.captured(2);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        LoaderVersion lv;
        lv.mc = m.captured(1);
        lv.version = m.captured(2);
        m_optifine << lv;
    }
}

QStringList VersionData::mcVersions(Loader loader, bool snapshots) const
{
    QStringList out;
    for (const auto &v : m_vanilla) {
        if (v.type == QLatin1String("snapshot") && !snapshots)
            continue;
        bool ok = false;
        switch (loader) {
        case Loader::Fabric: ok = m_fabricGames.contains(v.id); break;
        case Loader::Quilt: ok = m_quiltGames.contains(v.id); break;
        case Loader::Forge: ok = m_forge.contains(v.id); break;
        case Loader::NeoForge: ok = m_neo.contains(v.id); break;
        }
        if (ok)
            out << v.id;
    }
    return out;
}

QList<LoaderVersion> VersionData::loaderVersions(Loader loader, const QString &mc) const
{
    switch (loader) {
    case Loader::Fabric: return m_fabricGames.contains(mc) ? m_fabricLoaders : QList<LoaderVersion>();
    case Loader::Quilt: return m_quiltGames.contains(mc) ? m_quiltLoaders : QList<LoaderVersion>();
    case Loader::Forge: return m_forge.value(mc);
    case Loader::NeoForge: return m_neo.value(mc);
    }
    return {};
}

} // namespace MI
