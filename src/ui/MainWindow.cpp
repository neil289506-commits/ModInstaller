#include "MainWindow.h"
#include <functional>
#include "Flows.h"
#include "RequestToastHost.h"
#include "SettingsDialog.h"
#include "TransferPanel.h"
#include "core/Settings.h"
#include <QApplication>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibraryInfo>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>

namespace MI {

static QTranslator *g_appTr = nullptr;
static QTranslator *g_qtTr = nullptr;

void applyLanguage(const QString &lang)
{
    if (g_appTr) {
        QCoreApplication::removeTranslator(g_appTr);
        delete g_appTr;
        g_appTr = nullptr;
    }
    if (g_qtTr) {
        QCoreApplication::removeTranslator(g_qtTr);
        delete g_qtTr;
        g_qtTr = nullptr;
    }
    // 程式內的原始字串為繁體中文；其他語言需載入對應 qm
    if (lang != QLatin1String("zh_TW")) {
        g_appTr = new QTranslator(qApp);
        if (g_appTr->load(QStringLiteral(":/i18n/ModInstaller_%1.qm").arg(lang)))
            QCoreApplication::installTranslator(g_appTr);
    }
    g_qtTr = new QTranslator(qApp);
    if (g_qtTr->load(QStringLiteral("qtbase_%1").arg(lang), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(g_qtTr);
}

namespace {

QPixmap svgPixmap(const QString &path, int size)
{
    QSvgRenderer r(path);
    QPixmap pm(size * 2, size * 2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    r.render(&p);
    pm.setDevicePixelRatio(2.0);
    return pm;
}

class Tile : public QPushButton
{
public:
    Tile(const QString &icon, const QString &title, const QString &desc, QWidget *parent) : QPushButton(parent)
    {
        setObjectName(QStringLiteral("tile"));
        setCursor(Qt::PointingHandCursor);
        setMinimumSize(250, 170);
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(22, 20, 22, 18);
        lay->setSpacing(6);
        auto *ic = new QLabel(this);
        ic->setPixmap(svgPixmap(icon, 40));
        ic->setFixedHeight(42);
        ic->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *t = new QLabel(title, this);
        t->setObjectName(QStringLiteral("tileTitle"));
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *d = new QLabel(desc, this);
        d->setObjectName(QStringLiteral("tileDesc"));
        d->setWordWrap(true);
        d->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        d->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(ic);
        lay->addSpacing(4);
        lay->addWidget(t);
        lay->addWidget(d, 1);
    }
};

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ModInstaller"));
    resize(980, 660);
    setMinimumSize(860, 600);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("home"));
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *banner = new QWidget(central);
    banner->setObjectName(QStringLiteral("banner"));
    auto *bl = new QVBoxLayout(banner);
    bl->setContentsMargins(44, 34, 44, 30);
    bl->setSpacing(4);
    auto *title = new QLabel(QStringLiteral("ModInstaller"), banner);
    title->setObjectName(QStringLiteral("appTitle"));
    auto *sub = new QLabel(tr("建立、分享與更新 Minecraft 模組包"), banner);
    sub->setObjectName(QStringLiteral("appSubtitle"));
    bl->addWidget(title);
    bl->addWidget(sub);
    root->addWidget(banner);

    auto *gridHost = new QWidget(central);
    auto *grid = new QGridLayout(gridHost);
    grid->setContentsMargins(44, 30, 44, 20);
    grid->setSpacing(18);

    struct Def {
        const char *icon;
        QString title;
        QString desc;
        std::function<void()> action;
    };
    const QList<Def> defs = {
        {":/icons/update.svg", tr("更新模組"), tr("選擇 .modpak，向託管平台檢查更新並覆寫"), [this]() { Flows::updateMods(this); }},
        {":/icons/create.svg", tr("建立模組包"), tr("選擇載入器與版本，搜尋模組並自動補齊依賴"), [this]() { Flows::createModpack(this); }},
        {":/icons/request.svg", tr("請求模組包檔案"), tr("提供 .modpak，下載全部模組並打包成 zip"), [this]() { Flows::requestModpack(this); }},
        {":/icons/settings.svg", tr("設定"), tr("CurseForge API、語言切換"), [this]() { openSettings(); }},
        {":/icons/github.svg", tr("GitHub Repo"), tr("開啟專案原始碼與問題回報頁面"),
         []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/neil289506-commits/ModInstaller"))); }},
        {":/icons/edit.svg", tr("更新 pak"), tr("修改既有模組包的內容（新增 / 移除模組）"), [this]() { Flows::editPak(this); }},
    };
    for (int i = 0; i < defs.size(); ++i) {
        auto *tile = new Tile(QString::fromLatin1(defs[i].icon), defs[i].title, defs[i].desc, gridHost);
        connect(tile, &QPushButton::clicked, this, defs[i].action);
        grid->addWidget(tile, i / 3, i % 3);
    }
    for (int c = 0; c < 3; ++c)
        grid->setColumnStretch(c, 1);
    for (int r = 0; r < 2; ++r)
        grid->setRowStretch(r, 1);
    root->addWidget(gridHost, 1);

    auto *bottom = new QWidget(central);
    auto *bl2 = new QHBoxLayout(bottom);
    bl2->setContentsMargins(24, 0, 24, 16);
    auto *foot = new QLabel(QStringLiteral("v%1  ·  GPL-3.0  ·  Based on PrismLauncher").arg(QStringLiteral(APP_VERSION)), bottom);
    foot->setObjectName(QStringLiteral("footer"));
    bl2->addWidget(foot, 0, Qt::AlignBottom | Qt::AlignLeft);
    bl2->addStretch(1);
    bl2->addWidget(new TransferPanel(bottom), 0, Qt::AlignBottom | Qt::AlignRight);
    root->addWidget(bottom);

    m_toasts = new RequestToastHost(central);

    setCentralWidget(central);
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && dlg.languageChanged()) {
        applyLanguage(Settings::i().language());
        emit languageChanged();
    }
}

} // namespace MI
