// ModInstaller - Minecraft 模組包建立 / 更新工具（僅支援 Windows）
// 模組搜尋與依賴解析邏輯移植自 PrismLauncher（GPL-3.0），本專案以 GPL-3.0 授權。
#include "core/Settings.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QPointer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ModInstaller"));
    QApplication::setOrganizationName(QStringLiteral("ModInstaller"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Link, QColor(0x1b, 0xd9, 0x6a));
    pal.setColor(QPalette::LinkVisited, QColor(0x1b, 0xd9, 0x6a));
    QApplication::setPalette(pal);

    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    MI::applyLanguage(MI::Settings::i().language());

    QPointer<MI::MainWindow> win;
    std::function<void()> show = [&]() {
        auto *w = new MI::MainWindow;
        w->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(w, &MI::MainWindow::languageChanged, w, [&, w]() {
            show(); // 以新語言重新建立主畫面，再關閉舊視窗
            w->hide();
            w->deleteLater();
        });
        win = w;
        w->show();
    };
    show();
    return app.exec();
}
