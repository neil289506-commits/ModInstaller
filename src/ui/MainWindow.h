#pragma once
#include <QMainWindow>

namespace MI {

class RequestToastHost;

// 套用語言（zh_TW 為原始字串，en 載入英文翻譯）
void applyLanguage(const QString &lang);

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

signals:
    void languageChanged();

private:
    void openSettings();
    RequestToastHost *m_toasts = nullptr;
};

} // namespace MI
