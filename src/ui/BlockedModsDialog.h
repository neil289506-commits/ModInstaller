#pragma once
#include "core/PackJob.h"
#include <QDialog>
#include <QHash>

class QTableWidget;
class QLineEdit;
class QTimer;

namespace MI {

// 作者禁止第三方下載的 CurseForge 模組：引導使用者手動下載（行為類似 Prism 的 BlockedModsDialog）
class BlockedModsDialog : public QDialog
{
    Q_OBJECT
public:
    BlockedModsDialog(const QList<JobMod> &mods, const QList<int> &indices, QWidget *parent = nullptr);
    QHash<int, QString> result() const { return m_found; }

private slots:
    void scan();
    void browseDir();

private:
    void updateRow(int row);
    QList<JobMod> m_mods;
    QList<int> m_indices;
    QHash<int, QString> m_found;
    QTableWidget *m_table;
    QLineEdit *m_dir;
    QTimer *m_timer;
};

} // namespace MI
