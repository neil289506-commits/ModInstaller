#pragma once
#include "core/DependencyResolver.h"
#include <QDialog>

class QTreeWidget;

namespace MI {

// 確認要下載的模組與自動搜索到的依賴（對應 Prism 的 ReviewMessageBox / 依賴確認）
class DependencyReviewDialog : public QDialog
{
    Q_OBJECT
public:
    DependencyReviewDialog(const QList<Selected> &selected, const QList<ResolvedDep> &deps, const QStringList &failures,
                           QWidget *parent = nullptr);
    QList<Selected> result() const;

private:
    QList<Selected> m_selected;
    QList<ResolvedDep> m_deps;
    QTreeWidget *m_tree;
};

} // namespace MI
