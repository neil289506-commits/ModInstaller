#include "PakEditorDialog.h"
#include "FileTargetDialog.h"
#include "Flows.h"
#include "ModBrowserDialog.h"
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace MI {

PakEditorDialog::PakEditorDialog(const PakData &data, const QString &path, QWidget *parent)
    : QDialog(parent), m_data(data), m_path(path)
{
    setWindowTitle(tr("更新 pak"));
    setModal(true);
    resize(820, 560);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    for (const auto &m : m_data.mods) {
        JobMod j = JobMod::fromPak(m);
        j.needDownload = false; // 既有模組已有 sha256，不需重新下載
        m_jobs << j;
    }

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(12);
    auto *head = new QLabel(tr("編輯模組包"), this);
    head->setObjectName(QStringLiteral("heading"));
    lay->addWidget(head);
    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("muted"));
    lay->addWidget(m_info);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("模組"), tr("版本"), tr("平台")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    lay->addWidget(m_tree, 1);

    auto *btns = new QHBoxLayout;
    auto *add = new QPushButton(tr("新增模組…"), this);
    auto *rem = new QPushButton(tr("移除所選"), this);
    rem->setObjectName(QStringLiteral("secondary"));
    auto *close = new QPushButton(tr("關閉"), this);
    close->setObjectName(QStringLiteral("secondary"));
    auto *save = new QPushButton(tr("儲存模組包…"), this);
    save->setDefault(true);
    btns->addWidget(add);
    btns->addWidget(rem);
    btns->addStretch(1);
    btns->addWidget(close);
    btns->addWidget(save);
    lay->addLayout(btns);

    connect(add, &QPushButton::clicked, this, &PakEditorDialog::addMods);
    connect(rem, &QPushButton::clicked, this, &PakEditorDialog::removeMods);
    connect(save, &QPushButton::clicked, this, &PakEditorDialog::save);
    connect(close, &QPushButton::clicked, this, [this]() {
        if (m_dirty && QMessageBox::question(this, windowTitle(), tr("有尚未儲存的變更，確定要關閉嗎？")) != QMessageBox::Yes)
            return;
        reject();
    });
    refresh();
}

QList<InstalledMod> PakEditorDialog::installed() const
{
    QList<InstalledMod> l;
    for (const auto &j : m_jobs) {
        InstalledMod i;
        i.provider = j.provider;
        i.projectId = j.projectId;
        i.versionId = j.versionId;
        i.fileName = j.fileName;
        l << i;
    }
    return l;
}

void PakEditorDialog::refresh()
{
    m_tree->clear();
    for (const auto &j : m_jobs)
        new QTreeWidgetItem(m_tree, {j.name, j.versionNumber, providerName(j.provider)});
    m_info->setText(tr("%1 %2 · %3 · 共 %4 個模組")
                        .arg(loaderName(m_data.target.loader), m_data.target.loaderVersion, m_data.target.mcVersion)
                        .arg(QFileInfo(m_path).fileName())
                        .arg(m_jobs.size()));
}

void PakEditorDialog::addMods()
{
    ModBrowserDialog browser(m_data.target, installed(), this);
    if (browser.exec() != QDialog::Accepted)
        return;
    for (const auto &s : browser.result()) {
        JobMod j = JobMod::fromVersion(s.pack, s.version);
        j.needDownload = true;
        m_jobs << j;
    }
    m_dirty = true;
    refresh();
}

void PakEditorDialog::removeMods()
{
    QList<int> rows;
    for (auto *it : m_tree->selectedItems())
        rows << m_tree->indexOfTopLevelItem(it);
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows)
        m_jobs.removeAt(r);
    m_dirty = true;
    refresh();
}

void PakEditorDialog::save()
{
    FileTargetDialog ft(tr("儲存模組包"), FileTargetDialog::PakOnly, QFileInfo(m_path).completeBaseName(), QFileInfo(m_path).absolutePath(),
                        tr("預設會覆寫原本的檔案（會先備份成 .bak）；也可以改名稱或位置另存。"), this);
    if (ft.exec() != QDialog::Accepted)
        return;
    const QString out = ft.filePath();
    if (QFileInfo(out) == QFileInfo(m_path)) {
        QFile::remove(m_path + QStringLiteral(".bak"));
        QFile::copy(m_path, m_path + QStringLiteral(".bak"));
    }
    if (Flows::runPackJob(this, PackJob::Mode::Pak, m_data.target, ft.baseName(), out, m_jobs, tr("儲存模組包"))) {
        m_dirty = false;
        Flows::showDone(this, tr("儲存模組包"), out);
        accept();
    }
}

} // namespace MI
