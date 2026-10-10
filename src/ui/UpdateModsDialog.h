#pragma once
#include "core/Models.h"
#include <QDialog>

class QTreeWidget;

namespace MI {

struct UpdateCandidate {
    int modIndex = -1;
    QString name;
    QString oldVersion;
    ModVersion newVersion;
};

class UpdateModsDialog : public QDialog
{
    Q_OBJECT
public:
    UpdateModsDialog(const QList<UpdateCandidate> &list, int checkedTotal, const QStringList &errors, QWidget *parent = nullptr);
    QList<int> selectedRows() const; // 回傳 list 內的索引

private:
    QTreeWidget *m_tree;
};

} // namespace MI
