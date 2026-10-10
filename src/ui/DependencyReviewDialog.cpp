#include "DependencyReviewDialog.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace MI {

DependencyReviewDialog::DependencyReviewDialog(const QList<Selected> &selected, const QList<ResolvedDep> &deps, const QStringList &failures,
                                               QWidget *parent)
    : QDialog(parent), m_selected(selected), m_deps(deps)
{
    setWindowTitle(tr("確認要加入的模組"));
    setModal(true);
    resize(780, 520);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(12);
    auto *head = new QLabel(tr("確認要加入的模組"), this);
    head->setObjectName(QStringLiteral("heading"));
    lay->addWidget(head);
    auto *info = new QLabel(deps.isEmpty() ? tr("沒有找到需要額外加入的依賴模組。")
                                           : tr("已自動搜索到 %1 個必要的依賴模組，取消勾選可排除。").arg(deps.size()), this);
    info->setObjectName(QStringLiteral("muted"));
    info->setWordWrap(true);
    lay->addWidget(info);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("模組"), tr("版本"), tr("平台"), tr("被需要於")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    lay->addWidget(m_tree, 1);

    for (const auto &s : selected) {
        auto *it = new QTreeWidgetItem(m_tree, {s.pack.name, s.version.label(), providerName(s.pack.provider), QString()});
        it->setCheckState(0, Qt::Checked);
    }
    for (const auto &d : deps) {
        QString name = d.pack.name;
        if (d.maybeInstalled)
            name += tr("（可能已安裝）");
        auto *it = new QTreeWidgetItem(m_tree, {name, d.version.label(), providerName(d.pack.provider), d.requiredBy.join(QStringLiteral(", "))});
        it->setCheckState(0, d.maybeInstalled ? Qt::Unchecked : Qt::Checked);
        it->setForeground(0, QColor(0x1b, 0xd9, 0x6a));
    }

    if (!failures.isEmpty()) {
        auto *fl = new QLabel(tr("部分依賴無法取得：\n%1").arg(failures.join(QLatin1Char('\n'))), this);
        fl->setWordWrap(true);
        fl->setObjectName(QStringLiteral("warn"));
        lay->addWidget(fl);
    }

    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *cancel = new QPushButton(tr("返回"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    auto *ok = new QPushButton(tr("確定"), this);
    ok->setDefault(true);
    btns->addWidget(cancel);
    btns->addWidget(ok);
    lay->addLayout(btns);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
}

QList<Selected> DependencyReviewDialog::result() const
{
    QList<Selected> out;
    int row = 0;
    for (const auto &s : m_selected) {
        if (m_tree->topLevelItem(row)->checkState(0) == Qt::Checked)
            out << s;
        ++row;
    }
    for (const auto &d : m_deps) {
        if (m_tree->topLevelItem(row)->checkState(0) == Qt::Checked) {
            Selected s;
            s.pack = d.pack;
            s.version = d.version;
            out << s;
        }
        ++row;
    }
    return out;
}

} // namespace MI
