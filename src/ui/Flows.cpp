#include "Flows.h"
#include "BlockedModsDialog.h"
#include "FileTargetDialog.h"
#include "LoaderVersionDialog.h"
#include "ModBrowserDialog.h"
#include "PakEditorDialog.h"
#include "ProgressDialog.h"
#include "UpdateModsDialog.h"
#include "core/ModApi.h"
#include "core/ModPak.h"
#include "core/Settings.h"
#include "core/Util.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace MI {
namespace Flows {


void showDone(QWidget *parent, const QString &title, const QString &path)
{
    QMessageBox box(QMessageBox::Information, title, QCoreApplication::translate("Flows", "完成！\n%1").arg(QDir::toNativeSeparators(path)), QMessageBox::NoButton, parent);
    auto *open = box.addButton(QCoreApplication::translate("Flows", "開啟資料夾"), QMessageBox::ActionRole);
    box.addButton(QCoreApplication::translate("Flows", "關閉"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == open) {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    }
}

bool runPackJob(QWidget *parent, PackJob::Mode mode, const PackTarget &target, const QString &name, const QString &output,
                const QList<JobMod> &mods, const QString &title)
{
    PackJob job(mode, target, name, output, mods);
    ProgressDialog dlg(title, parent);
    dlg.setBusy();
    bool ok = false;
    bool cancelled = false;
    QString err;
    QObject::connect(&job, &PackJob::status, &dlg, &ProgressDialog::setStatus);
    QObject::connect(&job, &PackJob::progress, &dlg, [&dlg](int d, int t) { dlg.setProgress(d, t); });
    QObject::connect(&job, &PackJob::blockedMods, &dlg, [&](const QList<int> &idx) {
        BlockedModsDialog bd(job.mods(), idx, &dlg);
        if (bd.exec() == QDialog::Accepted) {
            job.provideBlocked(bd.result());
        } else {
            cancelled = true;
            job.cancel();
        }
    });
    QObject::connect(&job, &PackJob::finished, &dlg, [&](bool o, const QString &e) {
        ok = o;
        err = e;
        dlg.accept();
    });
    QObject::connect(&dlg, &ProgressDialog::cancelRequested, &job, [&]() {
        cancelled = true;
        job.cancel();
    });
    QTimer::singleShot(0, &job, &PackJob::start);
    dlg.exec();
    if (!ok && !cancelled)
        QMessageBox::warning(parent, title, err);
    return ok;
}

void createModpack(QWidget *parent)
{
    LoaderVersionDialog lv(parent);
    if (lv.exec() != QDialog::Accepted)
        return;
    const PackTarget target = lv.target();

    ModBrowserDialog browser(target, {}, parent);
    if (browser.exec() != QDialog::Accepted)
        return;
    const QList<Selected> sel = browser.result();

    const QString defName = QStringLiteral("ModPack-%1-%2").arg(loaderName(target.loader), target.mcVersion);
    FileTargetDialog ft(QCoreApplication::translate("Flows", "輸出模組包"), FileTargetDialog::ChooseFormat, defName, QString(),
                        QCoreApplication::translate("Flows", "共 %1 個模組（%2 %3，載入器版本 %4）。").arg(sel.size()).arg(loaderName(target.loader), target.mcVersion, target.loaderVersion),
                        parent);
    if (ft.exec() != QDialog::Accepted)
        return;

    QList<JobMod> mods;
    for (const auto &s : sel)
        mods << JobMod::fromVersion(s.pack, s.version);
    const auto mode = ft.isPak() ? PackJob::Mode::Pak : PackJob::Mode::Zip;
    if (runPackJob(parent, mode, target, ft.baseName(), ft.filePath(), mods, QCoreApplication::translate("Flows", "建立模組包")))
        showDone(parent, QCoreApplication::translate("Flows", "建立模組包"), ft.filePath());
}

static bool pickPak(QWidget *parent, QString *path, PakData *data)
{
    const QString p = QFileDialog::getOpenFileName(parent, QCoreApplication::translate("Flows", "選擇模組包檔案"), Settings::i().lastDir(QStringLiteral("pak")),
                                                   QCoreApplication::translate("Flows", "模組包 (*.modpak);;所有檔案 (*)"));
    if (p.isEmpty())
        return false;
    Settings::i().setLastDir(QStringLiteral("pak"), QFileInfo(p).absolutePath());
    QString err;
    if (!ModPak::read(p, data, &err)) {
        QMessageBox::warning(parent, QCoreApplication::translate("Flows", "模組包"), err);
        return false;
    }
    *path = p;
    return true;
}

void requestModpack(QWidget *parent)
{
    QString path;
    PakData data;
    if (!pickPak(parent, &path, &data))
        return;
    if (data.mods.isEmpty()) {
        QMessageBox::information(parent, QCoreApplication::translate("Flows", "請求模組包檔案"), QCoreApplication::translate("Flows", "這個模組包沒有任何模組。"));
        return;
    }
    const QString defName = data.name.isEmpty() ? QFileInfo(path).completeBaseName() : data.name;
    FileTargetDialog ft(QCoreApplication::translate("Flows", "輸出 zip"), FileTargetDialog::ZipOnly, defName, QString(),
                        QCoreApplication::translate("Flows", "模組包包含 %1 個模組（%2 %3），將依資料重新下載並驗證 sha256 後打包成 zip。")
                            .arg(data.mods.size())
                            .arg(loaderName(data.target.loader), data.target.mcVersion),
                        parent);
    if (ft.exec() != QDialog::Accepted)
        return;
    QList<JobMod> mods;
    for (const auto &m : data.mods) {
        JobMod j = JobMod::fromPak(m);
        j.needDownload = true;
        mods << j;
    }
    if (runPackJob(parent, PackJob::Mode::Zip, data.target, ft.baseName(), ft.filePath(), mods, QCoreApplication::translate("Flows", "請求模組包檔案")))
        showDone(parent, QCoreApplication::translate("Flows", "請求模組包檔案"), ft.filePath());
}

void updateMods(QWidget *parent)
{
    QString path;
    PakData data;
    if (!pickPak(parent, &path, &data))
        return;
    if (data.mods.isEmpty()) {
        QMessageBox::information(parent, QCoreApplication::translate("Flows", "更新模組"), QCoreApplication::translate("Flows", "這個模組包沒有任何模組。"));
        return;
    }

    // 向每個模組登記的託管平台請求更新
    struct State {
        int pending = 0;
        QList<UpdateCandidate> found;
        QStringList errors;
    };
    auto st = std::make_shared<State>();
    ProgressDialog prog(QCoreApplication::translate("Flows", "更新模組"), parent);
    prog.setStatus(QCoreApplication::translate("Flows", "正在向託管平台檢查更新…"));
    prog.setProgress(0, data.mods.size());
    st->pending = data.mods.size();
    bool cancelled = false;
    QObject::connect(&prog, &ProgressDialog::cancelRequested, &prog, [&]() {
        cancelled = true;
        prog.reject();
    });
    for (int i = 0; i < data.mods.size(); ++i) {
        const PakMod &m = data.mods[i];
        ModApi::versions(&prog, m.provider, m.projectId, data.target,
                         [&, st, i, m](bool ok, const QList<ModVersion> &list, const QString &err) {
                             if (!ok) {
                                 st->errors << QStringLiteral("%1: %2").arg(m.name, err);
                             } else {
                                 const ModVersion best = ModApi::best(list);
                                 ModVersion current;
                                 for (const auto &v : list)
                                     if (v.versionId == m.versionId)
                                         current = v;
                                 if (best.valid() && best.versionId != m.versionId && (!current.valid() || best.date > current.date)) {
                                     UpdateCandidate c;
                                     c.modIndex = i;
                                     c.name = m.name;
                                     c.oldVersion = m.versionNumber;
                                     c.newVersion = best;
                                     st->found << c;
                                 }
                             }
                             prog.setProgress(data.mods.size() - st->pending + 1, data.mods.size());
                             if (--st->pending == 0)
                                 prog.accept();
                         });
    }
    prog.exec();
    if (cancelled)
        return;
    std::sort(st->found.begin(), st->found.end(), [](const UpdateCandidate &a, const UpdateCandidate &b) { return a.modIndex < b.modIndex; });
    if (st->found.isEmpty()) {
        QString msg = QCoreApplication::translate("Flows", "所有模組都已是最新版本。");
        if (!st->errors.isEmpty())
            msg += QStringLiteral("\n\n") + QCoreApplication::translate("Flows", "以下模組檢查失敗：\n%1").arg(st->errors.join(QLatin1Char('\n')));
        QMessageBox::information(parent, QCoreApplication::translate("Flows", "更新模組"), msg);
        return;
    }

    UpdateModsDialog ud(st->found, data.mods.size(), st->errors, parent);
    if (ud.exec() != QDialog::Accepted)
        return;
    const QList<int> chosen = ud.selectedRows();
    if (chosen.isEmpty())
        return;

    FileTargetDialog ft(QCoreApplication::translate("Flows", "儲存更新後的模組包"), FileTargetDialog::PakOnly, QFileInfo(path).completeBaseName(), QFileInfo(path).absolutePath(),
                        QCoreApplication::translate("Flows", "預設會覆寫原本的檔案（會先備份成 .bak）；也可以改名稱或位置另存。"), parent);
    if (ft.exec() != QDialog::Accepted)
        return;

    QSet<int> updating;
    QHash<int, UpdateCandidate> byIndex;
    for (int r : chosen) {
        updating.insert(st->found[r].modIndex);
        byIndex.insert(st->found[r].modIndex, st->found[r]);
    }
    QList<JobMod> mods;
    for (int i = 0; i < data.mods.size(); ++i) {
        const PakMod &m = data.mods[i];
        if (updating.contains(i)) {
            ModPack p;
            p.provider = m.provider;
            p.addonId = m.projectId;
            p.name = m.name;
            p.websiteUrl = m.websiteUrl;
            JobMod j = JobMod::fromVersion(p, byIndex[i].newVersion);
            mods << j;
        } else {
            JobMod j = JobMod::fromPak(m);
            j.needDownload = false;
            mods << j;
        }
    }

    const QString out = ft.filePath();
    if (QFileInfo(out) == QFileInfo(path)) {
        QFile::remove(path + QStringLiteral(".bak"));
        QFile::copy(path, path + QStringLiteral(".bak"));
    }
    if (runPackJob(parent, PackJob::Mode::Pak, data.target, ft.baseName(), out, mods, QCoreApplication::translate("Flows", "更新模組")))
        showDone(parent, QCoreApplication::translate("Flows", "更新模組"), out);
}

void editPak(QWidget *parent)
{
    QString path;
    PakData data;
    if (!pickPak(parent, &path, &data))
        return;
    PakEditorDialog dlg(data, path, parent);
    dlg.exec();
}

} // namespace Flows
} // namespace MI
