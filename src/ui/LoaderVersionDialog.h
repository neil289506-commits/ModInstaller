#pragma once
#include "core/Models.h"
#include "core/VersionData.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace MI {

// 建立模組包第一步：選擇模組載入器 + 遊戲版本 + 載入器版本
class LoaderVersionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoaderVersionDialog(QWidget *parent = nullptr);
    PackTarget target() const;

private slots:
    void loadData();
    void onLoaderChanged();
    void refillMc();
    void refillLoaderVersions();

private:
    void setBusy(bool busy, const QString &text = QString());

    VersionData *m_data;
    QComboBox *m_loader;
    QComboBox *m_mc;
    QComboBox *m_lv;
    QCheckBox *m_snap;
    QLabel *m_status;
    QPushButton *m_refresh;
    QPushButton *m_ok;
};

} // namespace MI
