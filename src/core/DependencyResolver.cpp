#include "DependencyResolver.h"
#include "ModApi.h"
#include "Util.h"
#include <QTimer>
#include <algorithm>

namespace MI {

DependencyResolver::DependencyResolver(const PackTarget &target, const QList<Selected> &selected, const QList<InstalledMod> &installed,
                                       QObject *parent)
    : QObject(parent), m_target(target), m_selected(selected), m_installed(installed)
{
    for (const auto &m : m_installed)
        if (!m.fileName.isEmpty())
            m_installedFiles << m.fileName;
}

QList<DependencyResolver::OverrideDep> DependencyResolver::overrideDeps()
{
    // Fabric API ↔ Quilted Fabric API、Kotlin 函式庫（同 Prism ModPlatform::getOverrideDeps）
    return {
        {QStringLiteral("634179"), QStringLiteral("306612"), Provider::Flame},
        {QStringLiteral("720410"), QStringLiteral("308769"), Provider::Flame},
        {QStringLiteral("qvIfYCYJ"), QStringLiteral("P7dR8mSH"), Provider::Modrinth},
        {QStringLiteral("lwVhp9o5"), QStringLiteral("Ha28R6CL"), Provider::Modrinth},
    };
}

Dependency DependencyResolver::getOverride(const Dependency &dep, Provider provider) const
{
    const bool isQuilt = m_target.loader == Loader::Quilt;
    if (isQuilt || m_target.loader == Loader::Fabric) {
        for (const auto &o : overrideDeps()) {
            if (o.provider == provider && dep.addonId == (isQuilt ? o.fabric : o.quilt)) {
                Dependency d;
                d.addonId = isQuilt ? o.quilt : o.fabric;
                d.type = dep.type;
                return d;
            }
        }
    }
    return dep;
}

QList<Dependency> DependencyResolver::dependenciesForVersion(const ModVersion &v, Provider provider) const
{
    QList<Dependency> out;
    for (Dependency verDep : v.deps) {
        if (verDep.type != Dependency::Required)
            continue;
        verDep = getOverride(verDep, provider);
        const bool onlyVersion = provider == Provider::Modrinth && verDep.addonId.isEmpty();

        auto sameDep = [&](const Dependency &i) { return onlyVersion ? i.versionId == verDep.versionId : i.addonId == verDep.addonId; };
        if (std::any_of(out.begin(), out.end(), sameDep))
            continue; // 目前清單已有

        auto known = [&](const PackDepPtr &i) {
            return i->pack.provider == provider && (onlyVersion ? i->version.versionId == verDep.versionId : i->pack.addonId == verDep.addonId);
        };
        auto knownSel = [&](const Selected &s) {
            return s.pack.provider == provider && (onlyVersion ? s.version.versionId == verDep.versionId : s.pack.addonId == verDep.addonId);
        };
        if (std::any_of(m_selected.begin(), m_selected.end(), knownSel))
            continue; // 已選取
        auto isInstalled = [&](const InstalledMod &m) {
            return m.provider == provider && (onlyVersion ? m.versionId == verDep.versionId : m.projectId == verDep.addonId);
        };
        if (std::any_of(m_installed.begin(), m_installed.end(), isInstalled))
            continue; // 已存在於模組包
        if (std::any_of(m_packDeps.begin(), m_packDeps.end(), known))
            continue; // 已載入的依賴

        out << verDep;
    }
    return out;
}

void DependencyResolver::start()
{
    for (const auto &sel : m_selected) {
        // 同 Prism checkDependencies：版本需符合目標
        const bool ok = sel.pack.versions.isEmpty() || sel.version.mcVersions.contains(m_target.mcVersion) || sel.version.mcVersions.isEmpty();
        if (!ok)
            continue;
        for (const auto &dep : dependenciesForVersion(sel.version, sel.pack.provider))
            prepare(dep, sel.pack.provider, 20);
    }
    if (m_pending == 0)
        QTimer::singleShot(0, this, [this]() { finish(); });
}

void DependencyResolver::endTask()
{
    if (--m_pending == 0)
        finish();
}

void DependencyResolver::removePack(const QString &addonId)
{
    m_packDeps.erase(std::remove_if(m_packDeps.begin(), m_packDeps.end(), [&](const PackDepPtr &v) { return v->pack.addonId == addonId; }),
                     m_packDeps.end());
}

void DependencyResolver::fetchProjectInfo(const PackDepPtr &pDep)
{
    beginTask();
    ModApi::project(this, pDep->pack.provider, pDep->pack.addonId, [this, pDep](bool ok, const ModPack &info, const QString &) {
        if (ok) {
            pDep->pack.name = info.name;
            pDep->pack.slug = info.slug;
            pDep->pack.summary = info.summary;
            pDep->pack.author = info.author;
            pDep->pack.iconUrl = info.iconUrl;
            pDep->pack.websiteUrl = info.websiteUrl;
            pDep->pack.downloads = info.downloads;
        }
        endTask();
    });
}

void DependencyResolver::prepare(const Dependency &dep, Provider provider, int level)
{
    auto pDep = std::make_shared<PackDep>();
    pDep->dependency = dep;
    pDep->pack.addonId = dep.addonId;
    pDep->pack.provider = provider;
    m_packDeps.append(pDep);

    beginTask();
    if (!dep.addonId.isEmpty())
        fetchProjectInfo(pDep);

    ModApi::dependencyVersion(this, provider, dep, m_target, [=, this](bool ok, const ModVersion &version, const QString &err) {
        if (!ok || !version.valid() || version.addonId.isEmpty()) {
            // Quilt 後備：改找對應的 Fabric 依賴
            if (ok && m_target.loader == Loader::Quilt) {
                for (const auto &o : overrideDeps()) {
                    if (o.provider == provider && dep.addonId == o.quilt) {
                        removePack(dep.addonId);
                        Dependency d;
                        d.addonId = o.fabric;
                        d.type = dep.type;
                        prepare(d, provider, level);
                        endTask();
                        return;
                    }
                }
            }
            if (!ok || !version.valid())
                m_failures << QStringLiteral("%1: %2").arg(dep.addonId.isEmpty() ? dep.versionId : dep.addonId, err);
            removePack(dep.addonId);
            endTask();
            return;
        }

        pDep->version = version;
        pDep->pack.versions = {version};
        pDep->pack.versionsLoaded = true;

        if (level == 0) {
            removePack(dep.addonId);
            endTask();
            return;
        }
        if (dep.addonId.isEmpty() && !version.addonId.isEmpty()) {
            pDep->pack.addonId = version.addonId;
            Dependency probe;
            probe.addonId = version.addonId;
            probe.type = dep.type;
            const Dependency over = getOverride(probe, provider);
            if (over.addonId != version.addonId) {
                removePack(version.addonId);
                prepare(over, provider, level);
                endTask();
                return;
            }
            fetchProjectInfo(pDep);
        }
        if (isLocallyInstalled(pDep)) {
            removePack(version.addonId);
            endTask();
            return;
        }
        for (const auto &d : dependenciesForVersion(version, provider))
            prepare(d, provider, level - 1);
        endTask();
    });
}

bool DependencyResolver::isLocallyInstalled(const PackDepPtr &pDep) const
{
    const QString fn = pDep->version.fileName;
    if (fn.isEmpty())
        return true;
    for (const auto &s : m_selected)
        if (!s.version.fileName.isEmpty() && laxCompare(s.version.fileName, fn))
            return true;
    for (const auto &f : m_installedFiles)
        if (!f.isEmpty() && laxCompare(f, fn))
            return true;
    for (const auto &o : m_packDeps)
        if (pDep->pack.addonId != o->pack.addonId && !o->version.fileName.isEmpty() && laxCompare(fn, o->version.fileName))
            return true;
    return false;
}

bool DependencyResolver::maybeInstalled(const PackDepPtr &pDep) const
{
    for (const auto &f : m_installedFiles)
        if (!f.isEmpty() && laxCompare(f, pDep->version.fileName, true))
            return true;
    return false;
}

void DependencyResolver::finish()
{
    if (m_done)
        return;
    m_done = true;

    // 與已選取的模組合併後計算「被誰需要」
    QList<PackDepPtr> full;
    for (const auto &s : m_selected) {
        auto p = std::make_shared<PackDep>();
        p->pack = s.pack;
        p->version = s.version;
        full << p;
    }
    full += m_packDeps;

    QList<ResolvedDep> out;
    for (const auto &mod : m_packDeps) {
        if (!mod->version.valid())
            continue;
        bool dup = false;
        for (const auto &o : out)
            if (o.pack.provider == mod->pack.provider && o.pack.addonId == mod->pack.addonId)
                dup = true;
        if (dup)
            continue;

        ResolvedDep r;
        r.pack = mod->pack;
        r.version = mod->version;
        if (r.pack.name.isEmpty())
            r.pack.name = r.version.displayName.isEmpty() ? r.pack.addonId : r.version.displayName;
        for (const auto &other : full) {
            if (other->pack.provider != mod->pack.provider)
                continue;
            for (const auto &d : other->version.deps) {
                if (d.type != Dependency::Required)
                    continue;
                const bool onlyVersion = mod->pack.provider == Provider::Modrinth && d.addonId.isEmpty();
                const bool match = onlyVersion ? d.versionId == mod->version.versionId : d.addonId == mod->pack.addonId;
                if (match) {
                    r.requiredBy << other->pack.name;
                    break;
                }
            }
        }
        r.maybeInstalled = maybeInstalled(mod);
        out << r;
    }
    emit finished(out, m_failures);
}

} // namespace MI
