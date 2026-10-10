// ModInstaller - 資料模型
// 邏輯參考 PrismLauncher 的 ModPlatform::IndexedPack / IndexedVersion（GPL-3.0）
#pragma once
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace MI {

enum class Provider { Modrinth, Flame };

inline QString providerId(Provider p) { return p == Provider::Modrinth ? QStringLiteral("modrinth") : QStringLiteral("curseforge"); }
inline QString providerName(Provider p) { return p == Provider::Modrinth ? QStringLiteral("Modrinth") : QStringLiteral("CurseForge"); }
inline Provider providerFromId(const QString &s) { return s.toLower() == QLatin1String("modrinth") ? Provider::Modrinth : Provider::Flame; }

enum class Loader { Fabric, Quilt, Forge, NeoForge };

inline QString loaderId(Loader l)
{
    switch (l) {
    case Loader::Fabric: return QStringLiteral("fabric");
    case Loader::Quilt: return QStringLiteral("quilt");
    case Loader::Forge: return QStringLiteral("forge");
    case Loader::NeoForge: return QStringLiteral("neoforge");
    }
    return {};
}
inline QString loaderName(Loader l)
{
    switch (l) {
    case Loader::Fabric: return QStringLiteral("Fabric");
    case Loader::Quilt: return QStringLiteral("Quilt");
    case Loader::Forge: return QStringLiteral("Forge");
    case Loader::NeoForge: return QStringLiteral("NeoForge");
    }
    return {};
}
inline bool loaderFromId(const QString &s, Loader *out)
{
    const QString n = s.trimmed().toLower();
    for (Loader l : {Loader::Fabric, Loader::Quilt, Loader::Forge, Loader::NeoForge}) {
        if (loaderId(l) == n) {
            *out = l;
            return true;
        }
    }
    return false;
}
// CurseForge ModLoaderType
inline int flameLoaderCode(Loader l)
{
    switch (l) {
    case Loader::Forge: return 1;
    case Loader::Fabric: return 4;
    case Loader::Quilt: return 5;
    case Loader::NeoForge: return 6;
    }
    return 0;
}

// 建立模組包時的目標（遊戲版本 + 載入器 + 載入器版本）
struct PackTarget {
    QString mcVersion;
    Loader loader = Loader::Fabric;
    QString loaderVersion;

    // 同 Prism PackProfile::getSupportedModLoaders：Quilt 相容 Fabric；NeoForge 1.20.1 相容 Forge
    QList<Loader> acceptedLoaders() const
    {
        QList<Loader> l{loader};
        if (loader == Loader::Quilt)
            l << Loader::Fabric;
        if (loader == Loader::NeoForge && mcVersion == QLatin1String("1.20.1"))
            l << Loader::Forge;
        return l;
    }
};

struct Dependency {
    enum Type { Required, Optional, Embedded, Tool, Incompatible, Include, Unknown };
    QString addonId;   // 專案 ID（Modrinth 可能為空，只有 versionId）
    QString versionId; // 指定版本 ID（可能為空）
    Type type = Unknown;
};

struct ModVersion {
    QString addonId;
    QString versionId;
    QString versionNumber;
    QString displayName;
    QString fileName;
    QString downloadUrl; // CurseForge 可能為空（作者禁止第三方下載）
    QString sha1, sha512, md5;
    QString changelog;
    QStringList mcVersions;
    QStringList loaders; // 小寫 loader id
    QList<Dependency> deps;
    QDateTime date;
    int releaseType = 1; // 1 release / 2 beta / 3 alpha
    bool valid() const { return !versionId.isEmpty(); }
    QString label() const { return versionNumber.isEmpty() ? displayName : versionNumber; }
};

struct ModPack {
    Provider provider = Provider::Modrinth;
    QString addonId;
    QString name;
    QString slug;
    QString summary;
    QString author;
    QString iconUrl;
    QString websiteUrl;
    qint64 downloads = 0;
    QList<ModVersion> versions;
    bool versionsLoaded = false;
};

struct Selected {
    ModPack pack;
    ModVersion version;
};

// 已存在於模組包／資料夾內的模組（用於依賴判斷）
struct InstalledMod {
    Provider provider = Provider::Modrinth;
    QString projectId;
    QString versionId;
    QString fileName;
};

} // namespace MI
