#pragma once
#include "core/ModPak.h"
#include "core/PackJob.h"
#include <QDialog>

class QLabel;
class QTreeWidget;

namespace MI {

// 更新 pak：修改模組包內容（新增 / 移除模組），流程與 Prism 編輯模組相同
class PakEditorDialog : public QDialog
{
    Q_OBJECT
public:
    PakEditorDialog(const PakData &data, const QString &path, QWidget *parent = nullptr);

private slots:
    void addMods();
    void removeMods();
    void save();

private:
    void refresh();
    QList<InstalledMod> installed() const;

    PakData m_data;
    QString m_path;
    QList<JobMod> m_jobs; // 與畫面列表一一對應
    bool m_dirty = false;
    QTreeWidget *m_tree;
    QLabel *m_info;
};

} // namespace MI
