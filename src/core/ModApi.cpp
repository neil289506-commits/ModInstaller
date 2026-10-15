#include "ModApi.h"
#include "Net.h"
#include "Settings.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <algorithm>
#include <memory>

namespace MI {

namespace {

// 開發 / 測試用：可用環境變數改指向模擬伺服器
const QString kModrinth = qEnvironmentVariable("MODINSTALLER_MODRINTH_API", QStringLiteral("https://api.modrinth.com/v2"));
const QString kFlame = qEnvironmentVariable("MODINSTALLER_CURSEFORGE_API", QStringLiteral("https://api.curseforge.com/v1"));
constexpr int kFlameGame = 432;
constexpr int kFlameModClass = 6;


Headers flameHeaders()
{
    return {{"x-api-key", Settings::i().effectiveCurseforgeKey().toUtf8()}, {"Accept", "application/json"}};
}
Headers modrinthHeaders() { return {{"Accept", "application/json"}}; }

QString quoteList(const QStringList &l)
{
    QStringList q;
    for (const auto &s : l)
        q << QStringLiteral("\"%1\"").arg(s);
    return QStringLiteral("[") + q.join(QLatin1Char(',')) + QStringLiteral("]");
}

// ---------- Modrinth ----------
ModVersion parseModrinthVersion(const QJsonObject &o)
{
    ModVersion v;
    v.versionId = o.value("id").toString();
    v.addonId = o.value("project_id").toString();
    v.versionNumber = o.value("version_number").toString();
    v.displayName = o.value("name").toString();
    v.changelog = o.value("changelog").toString();
    v.date = QDateTime::fromString(o.value("date_published").toString(), Qt::ISODate);
    const QString type = o.value("version_type").toString();
    v.releaseType = type == QLatin1String("release") ? 1 : (type == QLatin1String("beta") ? 2 : 3);
    for (const auto &g : o.value("game_versions").toArray())
        v.mcVersions << g.toString();
    for (const auto &l : o.value("loaders").toArray())
        v.loaders << l.toString();

    const QJsonArray files = o.value("files").toArray();
    QJsonObject file;
    for (const auto &f : files) {
        if (f.toObject().value("primary").toBool()) {
            file = f.toObject();
            break;
        }
    }
    if (file.isEmpty() && !files.isEmpty())
        file = files.first().toObject();
    v.fileName = file.value("filename").toString();
    v.downloadUrl = file.value("url").toString();
    const QJsonObject hashes = file.value("hashes").toObject();
    v.sha1 = hashes.value("sha1").toString();
    v.sha512 = hashes.value("sha512").toString();

    for (const auto &d : o.value("dependencies").toArray()) {
        const QJsonObject dob = d.toObject();
        Dependency dep;
        dep.addonId = dob.value("project_id").toString();
        dep.versionId = dob.value("version_id").toString();
        const QString t = dob.value("dependency_type").toString();
        if (t == QLatin1String("required")) dep.type = Dependency::Required;
        else if (t == QLatin1String("optional")) dep.type = Dependency::Optional;
        else if (t == QLatin1String("incompatible")) dep.type = Dependency::Incompatible;
        else if (t == QLatin1String("embedded")) dep.type = Dependency::Embedded;
        else dep.type = Dependency::Unknown;
        v.deps << dep;
    }
    return v;
}

ModPack parseModrinthPack(const QJsonObject &o, bool fromSearch)
{
    ModPack p;
    p.provider = Provider::Modrinth;
    p.addonId = o.value(fromSearch ? "project_id" : "id").toString();
    p.name = o.value("title").toString();
    p.slug = o.value("slug").toString();
    p.summary = o.value("description").toString();
    p.author = o.value("author").toString();
    p.iconUrl = o.value("icon_url").toString();
    p.downloads = qint64(o.value("downloads").toDouble());
    const QString type = o.value("project_type").toString(QStringLiteral("mod"));
    p.websiteUrl = QStringLiteral("https://modrinth.com/%1/%2").arg(type, p.slug.isEmpty() ? p.addonId : p.slug);
    return p;
}

// ---------- CurseForge ----------
ModVersion parseFlameFile(const QJsonObject &o)
{
    ModVersion v;
    v.versionId = QString::number(qint64(o.value("id").toDouble()));
    v.addonId = QString::number(qint64(o.value("modId").toDouble()));
    v.displayName = o.value("displayName").toString();
    v.fileName = o.value("fileName").toString();
    v.versionNumber = v.displayName;
    v.downloadUrl = o.value("downloadUrl").isNull() ? QString() : o.value("downloadUrl").toString();
    v.date = QDateTime::fromString(o.value("fileDate").toString(), Qt::ISODate);
    v.releaseType = o.value("releaseType").toInt(1);
    for (const auto &h : o.value("hashes").toArray()) {
        const QJsonObject ho = h.toObject();
        const int algo = ho.value("algo").toInt();
        if (algo == 1) v.sha1 = ho.value("value").toString();
        else if (algo == 2) v.md5 = ho.value("value").toString();
    }
    static const QStringList known = {QStringLiteral("fabric"), QStringLiteral("quilt"), QStringLiteral("forge"),
                                      QStringLiteral("neoforge"), QStringLiteral("liteloader"), QStringLiteral("rift")};
    for (const auto &g : o.value("gameVersions").toArray()) {
        const QString s = g.toString();
        if (s.isEmpty())
            continue;
        if (s.at(0).isDigit())
            v.mcVersions << s;
        else if (known.contains(s.toLower()))
            v.loaders << s.toLower();
    }
    for (const auto &d : o.value("dependencies").toArray()) {
        const QJsonObject dob = d.toObject();
        Dependency dep;
        dep.addonId = QString::number(qint64(dob.value("modId").toDouble()));
        switch (dob.value("relationType").toInt()) {
        case 1: dep.type = Dependency::Embedded; break;
        case 2: dep.type = Dependency::Optional; break;
        case 3: dep.type = Dependency::Required; break;
        case 4: dep.type = Dependency::Tool; break;
        case 5: dep.type = Dependency::Incompatible; break;
        case 6: dep.type = Dependency::Include; break;
        default: dep.type = Dependency::Unknown; break;
        }
        v.deps << dep;
    }
    return v;
}

ModPack parseFlamePack(const QJsonObject &o)
{
    ModPack p;
    p.provider = Provider::Flame;
    p.addonId = QString::number(qint64(o.value("id").toDouble()));
    p.name = o.value("name").toString();
    p.slug = o.value("slug").toString();
    p.summary = o.value("summary").toString();
    const QJsonArray authors = o.value("authors").toArray();
    if (!authors.isEmpty())
        p.author = authors.first().toObject().value("name").toString();
    const QJsonObject logo = o.value("logo").toObject();
    p.iconUrl = logo.value("thumbnailUrl").toString();
    if (p.iconUrl.isEmpty())
        p.iconUrl = logo.value("url").toString();
    p.downloads = qint64(o.value("downloadCount").toDouble());
    p.websiteUrl = o.value("links").toObject().value("websiteUrl").toString();
    return p;
}

QJsonObject parseObj(const QByteArray &data)
{
    return QJsonDocument::fromJson(data).object();
}

void sortByDate(QList<ModVersion> &l)
{
    std::stable_sort(l.begin(), l.end(), [](const ModVersion &a, const ModVersion &b) { return a.date > b.date; });
}

} // namespace

bool ModApi::hasFlameKey() { return !Settings::i().effectiveCurseforgeKey().isEmpty(); }
QString ModApi::flameKeyMissingText() { return QCoreApplication::translate("ModApi", "尚未設定 CurseForge API Key，請至「設定」填寫。"); }

QList<SortMethod> ModApi::sortingMethods(Provider p)
{
    if (p == Provider::Modrinth) {
        return {{1, "relevance", QCoreApplication::translate("ModApi", "依相關性排序")}, {2, "downloads", QCoreApplication::translate("ModApi", "依下載數排序")}, {3, "follows", QCoreApplication::translate("ModApi", "依追蹤數排序")},
                {4, "newest", QCoreApplication::translate("ModApi", "依最新排序")},    {5, "updated", QCoreApplication::translate("ModApi", "依最後更新排序")}};
    }
    return {{1, "Featured", QCoreApplication::translate("ModApi", "依精選排序")},  {2, "Popularity", QCoreApplication::translate("ModApi", "依人氣排序")}, {3, "LastUpdated", QCoreApplication::translate("ModApi", "依最後更新排序")},
            {4, "Name", QCoreApplication::translate("ModApi", "依名稱排序")},     {5, "Author", QCoreApplication::translate("ModApi", "依作者排序")},     {6, "TotalDownloads", QCoreApplication::translate("ModApi", "依下載數排序")}};
}

ModVersion ModApi::best(const QList<ModVersion> &list)
{
    ModVersion bestV;
    for (const auto &v : list) {
        if (!bestV.valid()) {
            bestV = v;
            continue;
        }
        const bool vBetterType = v.releaseType < bestV.releaseType;
        const bool sameType = v.releaseType == bestV.releaseType;
        if (vBetterType || (sameType && v.date > bestV.date))
            bestV = v;
    }
    return bestV;
}

void ModApi::search(QObject *ctx, const SearchQuery &q, SearchCb cb)
{
    if (q.provider == Provider::Modrinth) {
        QStringList groups;
        QStringList loaders;
        for (Loader l : q.target.acceptedLoaders())
            loaders << QStringLiteral("\"categories:%1\"").arg(loaderId(l));
        groups << QStringLiteral("[%1]").arg(loaders.join(QLatin1Char(',')));
        if (!q.target.mcVersion.isEmpty())
            groups << QStringLiteral("[\"versions:%1\"]").arg(q.target.mcVersion);
        groups << QStringLiteral("[\"project_type:mod\"]");

        const auto sorts = sortingMethods(Provider::Modrinth);
        QString index = QStringLiteral("relevance");
        for (const auto &s : sorts)
            if (s.index == q.sortIndex)
                index = s.name;

        QUrl url(kModrinth + QStringLiteral("/search"));
        QUrlQuery qq;
        if (!q.text.isEmpty())
            qq.addQueryItem(QStringLiteral("query"), q.text);
        qq.addQueryItem(QStringLiteral("offset"), QString::number(q.offset));
        qq.addQueryItem(QStringLiteral("limit"), QString::number(PageSize));
        qq.addQueryItem(QStringLiteral("index"), index);
        qq.addQueryItem(QStringLiteral("facets"), QStringLiteral("[%1]").arg(groups.join(QLatin1Char(','))));
        url.setQuery(qq);

        Net::i().getBytes(url, modrinthHeaders(), ctx, [cb](bool ok, const QByteArray &d, const QString &err) {
            if (!ok) {
                cb(false, {}, 0, err);
                return;
            }
            const QJsonObject o = parseObj(d);
            QList<ModPack> packs;
            for (const auto &h : o.value("hits").toArray())
                packs << parseModrinthPack(h.toObject(), true);
            cb(true, packs, o.value("total_hits").toInt(), QString());
        });
        return;
    }

    if (!hasFlameKey()) {
        cb(false, {}, 0, flameKeyMissingText());
        return;
    }
    QUrl url(kFlame + QStringLiteral("/mods/search"));
    QUrlQuery qq;
    qq.addQueryItem(QStringLiteral("gameId"), QString::number(kFlameGame));
    qq.addQueryItem(QStringLiteral("classId"), QString::number(kFlameModClass));
    qq.addQueryItem(QStringLiteral("index"), QString::number(q.offset));
    qq.addQueryItem(QStringLiteral("pageSize"), QString::number(PageSize));
    if (!q.text.isEmpty())
        qq.addQueryItem(QStringLiteral("searchFilter"), q.text);
    qq.addQueryItem(QStringLiteral("sortField"), QString::number(q.sortIndex));
    qq.addQueryItem(QStringLiteral("sortOrder"), QStringLiteral("desc"));
    QStringList codes;
    for (Loader l : q.target.acceptedLoaders())
        codes << QString::number(flameLoaderCode(l));
    qq.addQueryItem(QStringLiteral("modLoaderTypes"), QStringLiteral("[%1]").arg(codes.join(QLatin1Char(','))));
    if (!q.target.mcVersion.isEmpty())
        qq.addQueryItem(QStringLiteral("gameVersion"), q.target.mcVersion);
    url.setQuery(qq);

    Net::i().getBytes(url, flameHeaders(), ctx, [cb](bool ok, const QByteArray &d, const QString &err) {
        if (!ok) {
            cb(false, {}, 0, err);
            return;
        }
        const QJsonObject o = parseObj(d);
        QList<ModPack> packs;
        for (const auto &h : o.value("data").toArray())
            packs << parseFlamePack(h.toObject());
        cb(true, packs, o.value("pagination").toObject().value("totalCount").toInt(), QString());
    });
}

void ModApi::versions(QObject *ctx, Provider p, const QString &addonId, const PackTarget &t, VersionsCb cb)
{
    if (p == Provider::Modrinth) {
        QStringList loaders;
        for (Loader l : t.acceptedLoaders())
            loaders << loaderId(l);
        QUrl url(kModrinth + QStringLiteral("/project/%1/version").arg(addonId));
        QUrlQuery qq;
        qq.addQueryItem(QStringLiteral("loaders"), quoteList(loaders));
        if (!t.mcVersion.isEmpty())
            qq.addQueryItem(QStringLiteral("game_versions"), quoteList({t.mcVersion}));
        url.setQuery(qq);
        Net::i().getBytes(url, modrinthHeaders(), ctx, [cb](bool ok, const QByteArray &d, const QString &err) {
            if (!ok) {
                cb(false, {}, err);
                return;
            }
            QList<ModVersion> list;
            for (const auto &v : QJsonDocument::fromJson(d).array())
                list << parseModrinthVersion(v.toObject());
            sortByDate(list);
            cb(true, list, QString());
        });
        return;
    }

    if (!hasFlameKey()) {
        cb(false, {}, flameKeyMissingText());
        return;
    }
    // CurseForge 的 modLoaderType 只能單選，所以每個相容載入器各查一次再合併（同 Prism）
    struct State {
        QList<ModVersion> all;
        int pending = 0;
        bool failed = false;
        QString err;
    };
    auto st = std::make_shared<State>();
    const auto loaders = t.acceptedLoaders();
    st->pending = loaders.size();
    for (Loader l : loaders) {
        QUrl url(kFlame + QStringLiteral("/mods/%1/files").arg(addonId));
        QUrlQuery qq;
        qq.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("10000"));
        if (!t.mcVersion.isEmpty())
            qq.addQueryItem(QStringLiteral("gameVersion"), t.mcVersion);
        qq.addQueryItem(QStringLiteral("modLoaderType"), QString::number(flameLoaderCode(l)));
        url.setQuery(qq);
        Net::i().getBytes(url, flameHeaders(), ctx, [st, cb](bool ok, const QByteArray &d, const QString &err) {
            if (!ok) {
                st->failed = true;
                st->err = err;
            } else {
                for (const auto &f : parseObj(d).value("data").toArray()) {
                    const ModVersion v = parseFlameFile(f.toObject());
                    bool dup = false;
                    for (const auto &e : st->all)
                        if (e.versionId == v.versionId) {
                            dup = true;
                            break;
                        }
                    if (!dup)
                        st->all << v;
                }
            }
            if (--st->pending == 0) {
                if (st->failed && st->all.isEmpty()) {
                    cb(false, {}, st->err);
                    return;
                }
                sortByDate(st->all);
                cb(true, st->all, QString());
            }
        });
    }
}

void ModApi::dependencyVersion(QObject *ctx, Provider p, const Dependency &dep, const PackTarget &t, VersionCb cb)
{
    if (p == Provider::Modrinth && !dep.versionId.isEmpty()) {
        Net::i().getBytes(QUrl(kModrinth + QStringLiteral("/version/%1").arg(dep.versionId)), modrinthHeaders(), ctx,
                          [cb](bool ok, const QByteArray &d, const QString &err) {
                              if (!ok) {
                                  cb(false, {}, err);
                                  return;
                              }
                              cb(true, parseModrinthVersion(parseObj(d)), QString());
                          });
        return;
    }
    if (dep.addonId.isEmpty()) {
        cb(false, {}, QCoreApplication::translate("ModApi", "相依性缺少專案 ID"));
        return;
    }
    versions(ctx, p, dep.addonId, t, [cb](bool ok, const QList<ModVersion> &list, const QString &err) {
        if (!ok) {
            cb(false, {}, err);
            return;
        }
        const ModVersion b = best(list);
        if (!b.valid()) {
            cb(false, {}, QCoreApplication::translate("ModApi", "找不到符合條件的版本"));
            return;
        }
        cb(true, b, QString());
    });
}

void ModApi::project(QObject *ctx, Provider p, const QString &addonId, PackCb cb)
{
    if (p == Provider::Modrinth) {
        Net::i().getBytes(QUrl(kModrinth + QStringLiteral("/project/%1").arg(addonId)), modrinthHeaders(), ctx,
                          [cb](bool ok, const QByteArray &d, const QString &err) {
                              if (!ok) {
                                  cb(false, {}, err);
                                  return;
                              }
                              cb(true, parseModrinthPack(parseObj(d), false), QString());
                          });
        return;
    }
    if (!hasFlameKey()) {
        cb(false, {}, flameKeyMissingText());
        return;
    }
    Net::i().getBytes(QUrl(kFlame + QStringLiteral("/mods/%1").arg(addonId)), flameHeaders(), ctx,
                      [cb](bool ok, const QByteArray &d, const QString &err) {
                          if (!ok) {
                              cb(false, {}, err);
                              return;
                          }
                          cb(true, parseFlamePack(parseObj(d).value("data").toObject()), QString());
                      });
}

void ModApi::resolveDownloadUrl(QObject *ctx, Provider p, const ModVersion &v, UrlCb cb)
{
    if (!v.downloadUrl.isEmpty()) {
        cb(true, v.downloadUrl, QString());
        return;
    }
    if (p != Provider::Flame) {
        cb(false, QString(), QCoreApplication::translate("ModApi", "沒有下載網址"));
        return;
    }
    Net::i().getBytes(QUrl(kFlame + QStringLiteral("/mods/%1/files/%2/download-url").arg(v.addonId, v.versionId)), flameHeaders(), ctx,
                      [cb](bool ok, const QByteArray &d, const QString &err) {
                          if (!ok) {
                              cb(false, QString(), err);
                              return;
                          }
                          const QString url = parseObj(d).value("data").toString();
                          if (url.isEmpty())
                              cb(false, QString(), QCoreApplication::translate("ModApi", "作者禁止第三方下載"));
                          else
                              cb(true, url, QString());
                      });
}

} // namespace MI
