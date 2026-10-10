#pragma once
#include "core/Models.h"
#include <QDialog>
#include <QMap>
#include <QStyledItemDelegate>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabBar;
class QToolButton;

namespace MI {

class ResultDelegate;

// 模組搜尋 / 選取視窗（介面與流程對應 Prism 的「下載模組」對話框）
class ModBrowserDialog : public QDialog
{
    Q_OBJECT
public:
    ModBrowserDialog(const PackTarget &target, const QList<InstalledMod> &installed, QWidget *parent = nullptr);
    QList<Selected> result() const { return m_result; }

private slots:
    void startSearch();
    void onProviderChanged(int idx);
    void onRowChanged(int row);
    void onVersionChanged(int idx);
    void toggleSelect();
    void removeSelected();
    void confirm();

private:
    void loadMore();
    void appendResults(const QList<ModPack> &packs);
    void showPack(int row);
    void populateVersions();
    void updateSelectButton();
    void refreshSelectedList();
    bool isInstalled(const ModPack &p) const;
    QString keyOf(const ModPack &p) const { return providerId(p.provider) + QLatin1Char(':') + p.addonId; }
    Provider provider() const { return m_tabs->currentIndex() == 0 ? Provider::Modrinth : Provider::Flame; }

    PackTarget m_target;
    QList<InstalledMod> m_installed;
    QList<ModPack> m_packs;
    QMap<QString, Selected> m_sel;
    QStringList m_selOrder;
    QList<Selected> m_result;
    int m_total = 0;
    int m_generation = 0;
    bool m_loading = false;

    QTabBar *m_tabs;
    QLineEdit *m_search;
    QComboBox *m_sort;
    QListWidget *m_list;
    QLabel *m_status;
    // 詳細資訊
    QLabel *m_icon;
    QLabel *m_name;
    QLabel *m_meta;
    QLabel *m_summary;
    QLabel *m_link;
    QComboBox *m_versions;
    QPushButton *m_selectBtn;
    QLabel *m_versionInfo;
    QListWidget *m_selList;
    QPushButton *m_removeBtn;
    QPushButton *m_confirmBtn;
    ResultDelegate *m_delegate;
};

} // namespace MI
