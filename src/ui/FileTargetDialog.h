#pragma once
#include <QDialog>

class QLabel;
class QLineEdit;
class QRadioButton;
class QPushButton;

namespace MI {

// 輸出檔案時，讓使用者輸入名稱與儲存位置（可選擇 zip / modpak）
class FileTargetDialog : public QDialog
{
    Q_OBJECT
public:
    enum Kind { ChooseFormat, ZipOnly, PakOnly };
    FileTargetDialog(const QString &title, Kind kind, const QString &defaultName, const QString &defaultDir, const QString &info,
                     QWidget *parent = nullptr);

    QString filePath() const;
    bool isPak() const;
    QString baseName() const;

private slots:
    void browse();
    void refreshPreview();
    void tryAccept();

private:
    Kind m_kind;
    QRadioButton *m_zip = nullptr;
    QRadioButton *m_pak = nullptr;
    QLineEdit *m_name;
    QLineEdit *m_dir;
    QLabel *m_preview;
    QPushButton *m_ok;
};

} // namespace MI
