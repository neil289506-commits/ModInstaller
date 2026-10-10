#pragma once
#include "Models.h"
#include <QObject>
#include <functional>

namespace MI {

struct SearchQuery {
    Provider provider = Provider::Modrinth;
    QString text;
    PackTarget target;
    int sortIndex = 1;
    int offset = 0;
};

struct SortMethod {
    int index;
    QString name;
    QString readable;
};

// Modrinth / CurseForge 統一介面（請求格式對應 Prism 的 ModrinthAPI / FlameAPI）
class ModApi
{
public:
    static constexpr int PageSize = 25;

    static QList<SortMethod> sortingMethods(Provider p);

    using SearchCb = std::function<void(bool ok, const QList<ModPack> &packs, int total, const QString &err)>;
    static void search(QObject *ctx, const SearchQuery &q, SearchCb cb);

    using VersionsCb = std::function<void(bool ok, const QList<ModVersion> &versions, const QString &err)>;
    static void versions(QObject *ctx, Provider p, const QString &addonId, const PackTarget &t, VersionsCb cb);

    using VersionCb = std::function<void(bool ok, const ModVersion &version, const QString &err)>;
    // 對應 Prism ResourceAPI::getDependencyVersion
    static void dependencyVersion(QObject *ctx, Provider p, const Dependency &dep, const PackTarget &t, VersionCb cb);

    using PackCb = std::function<void(bool ok, const ModPack &pack, const QString &err)>;
    static void project(QObject *ctx, Provider p, const QString &addonId, PackCb cb);

    using UrlCb = std::function<void(bool ok, const QString &url, const QString &err)>;
    // CurseForge 的 downloadUrl 可能為 null，需另外查詢；查不到代表作者禁止第三方下載
    static void resolveDownloadUrl(QObject *ctx, Provider p, const ModVersion &v, UrlCb cb);

    // 取得「最佳」版本：優先正式版，其次為較新的日期
    static ModVersion best(const QList<ModVersion> &list);

    static bool hasFlameKey();
    static QString flameKeyMissingText();
};

} // namespace MI
