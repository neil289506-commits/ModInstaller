#pragma once
#include "Models.h"
#include <QObject>
#include <memory>

namespace MI {

struct ResolvedDep {
    ModPack pack;
    ModVersion version;
    QStringList requiredBy;
    bool maybeInstalled = false;
};

// 自動搜索依賴模組，邏輯移植自 Prism GetModDependenciesTask（GPL-3.0）
class DependencyResolver : public QObject
{
    Q_OBJECT
public:
    DependencyResolver(const PackTarget &target, const QList<Selected> &selected, const QList<InstalledMod> &installed,
                       QObject *parent = nullptr);
    void start();

signals:
    void finished(const QList<MI::ResolvedDep> &deps, const QStringList &failures);

private:
    struct PackDep {
        Dependency dependency;
        ModPack pack;
        ModVersion version;
    };
    using PackDepPtr = std::shared_ptr<PackDep>;
    struct OverrideDep {
        QString quilt, fabric;
        Provider provider;
    };

    static QList<OverrideDep> overrideDeps();
    Dependency getOverride(const Dependency &dep, Provider provider) const;
    QList<Dependency> dependenciesForVersion(const ModVersion &v, Provider provider) const;
    void prepare(const Dependency &dep, Provider provider, int level);
    void fetchProjectInfo(const PackDepPtr &pDep);
    void removePack(const QString &addonId);
    bool isLocallyInstalled(const PackDepPtr &pDep) const;
    bool maybeInstalled(const PackDepPtr &pDep) const;
    void beginTask() { ++m_pending; }
    void endTask();
    void finish();

    PackTarget m_target;
    QList<Selected> m_selected;
    QList<InstalledMod> m_installed;
    QStringList m_installedFiles;
    QList<PackDepPtr> m_packDeps;
    QStringList m_failures;
    int m_pending = 0;
    bool m_done = false;
};

} // namespace MI
