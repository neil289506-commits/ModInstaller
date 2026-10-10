#include "UpdateModsDialog.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace MI {

UpdateModsDialog::UpdateModsDialog(const QList<UpdateCandidate> &list, int checkedTotal, const QStringList &errors, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("更新模組"));
    setModal(true);
    resize(720, 480);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(12);
    auto *head = new QLabel(tr("發現 %1 個模組有新版本").arg(list.size()), this);
    head->setObjectName(QStringLiteral("heading"));
    lay->addWidget(head);
    auto *info = new QLabel(tr("共檢查 %1 個模組。勾選要更新的項目，確認後會覆寫模組包檔案。").arg(checkedTotal), this);
    info->setObjectName(QStringLiteral("muted"));
    info->setWordWrap(true);
    lay->addWidget(info);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("模組"), tr("目前版本"), tr("新版本")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (const auto &c : list) {
        auto *it = new QTreeWidgetItem(m_tree, {c.name, c.oldVersion, c.newVersion.label()});
        it->setCheckState(0, Qt::Checked);
    }
    lay->addWidget(m_tree, 1);

    if (!errors.isEmpty()) {
        auto *el = new QLabel(tr("以下模組檢查失敗：\n%1").arg(errors.join(QLatin1Char('\n'))), this);
        el->setWordWrap(true);
        el->setObjectName(QStringLiteral("warn"));
        lay->addWidget(el);
    }

    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    auto *ok = new QPushButton(tr("更新所選項目"), this);
    ok->setDefault(true);
    btns->addWidget(cancel);
    btns->addWidget(ok);
    lay->addLayout(btns);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
}

QList<int> UpdateModsDialog::selectedRows() const
{
    QList<int> out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (m_tree->topLevelItem(i)->checkState(0) == Qt::Checked)
            out << i;
    return out;
}

} // namespace MI
