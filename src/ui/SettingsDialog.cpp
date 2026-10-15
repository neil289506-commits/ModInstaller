#include "SettingsDialog.h"
#include "core/DeviceCode.h"
#include "core/Net.h"
#include "core/PeerService.h"
#include "core/Settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace MI {

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("設定"));
    setModal(true);
    setMinimumWidth(520);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(10);
    auto *head = new QLabel(tr("設定"), this);
    head->setObjectName(QStringLiteral("heading"));
    lay->addWidget(head);

    auto *kl = new QLabel(tr("CurseForge API Key"), this);
    kl->setObjectName(QStringLiteral("fieldLabel"));
    lay->addWidget(kl);
    m_key = new QLineEdit(Settings::i().curseforgeKey(), this);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(Settings::i().hasEmbeddedCurseforgeKey() ? tr("留空使用程式內建的 Key；也可以貼上你自己的 API Key")
                                                                       : tr("貼上你的 API Key（用於搜尋 / 下載 CurseForge 模組）"));
    auto *show = new QCheckBox(tr("顯示"), this);
    auto *test = new QPushButton(tr("測試"), this);
    test->setObjectName(QStringLiteral("secondary"));
    auto *row = new QHBoxLayout;
    row->addWidget(m_key, 1);
    row->addWidget(show);
    row->addWidget(test);
    lay->addLayout(row);
    m_testResult = new QLabel(this);
    m_testResult->setWordWrap(true);
    lay->addWidget(m_testResult);
    auto *link = new QLabel(QStringLiteral("<a href=\"https://console.curseforge.com/\">%1</a>").arg(tr("前往 CurseForge 開發者後台申請 API Key")), this);
    link->setOpenExternalLinks(true);
    lay->addWidget(link);

    lay->addSpacing(8);
    auto *ll = new QLabel(tr("語言（i18n）"), this);
    ll->setObjectName(QStringLiteral("fieldLabel"));
    lay->addWidget(ll);
    m_lang = new QComboBox(this);
    m_lang->addItem(QStringLiteral("繁體中文"), QStringLiteral("zh_TW"));
    m_lang->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_lang->setCurrentIndex(qMax(0, m_lang->findData(Settings::i().language())));
    lay->addWidget(m_lang);

    lay->addSpacing(8);
    auto *rl = new QLabel(tr("模組包傳輸：Cloudflare 註冊服務網址"), this);
    rl->setObjectName(QStringLiteral("fieldLabel"));
    lay->addWidget(rl);
    m_registry = new QLineEdit(Settings::i().registryUrl(), this);
    m_registry->setPlaceholderText(tr("https://你的服務.workers.dev（用來確認 12 碼代碼不重複）"));
    lay->addWidget(m_registry);
    auto *dl = new QLabel(tr("本機代碼儲存位置：%1").arg(QDir::toNativeSeparators(DeviceCode::filePath()).toHtmlEscaped()), this);
    dl->setObjectName(QStringLiteral("muted"));
    dl->setWordWrap(true);
    lay->addWidget(dl);

    lay->addSpacing(10);
    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    auto *ok = new QPushButton(tr("儲存"), this);
    ok->setDefault(true);
    btns->addWidget(cancel);
    btns->addWidget(ok);
    lay->addLayout(btns);

    connect(show, &QCheckBox::toggled, this, [this](bool on) { m_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    connect(test, &QPushButton::clicked, this, &SettingsDialog::testKey);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &SettingsDialog::save);
}

void SettingsDialog::testKey()
{
    QString key = m_key->text().trimmed();
    if (key.isEmpty())
        key = Settings::i().effectiveCurseforgeKey(); // 留空 = 測試內建的 Key
    if (key.isEmpty()) {
        m_testResult->setText(tr("請先輸入 API Key"));
        return;
    }
    m_testResult->setText(tr("測試中…"));
    Net::i().getBytes(QUrl(QStringLiteral("https://api.curseforge.com/v1/games/432")), {{"x-api-key", key.toUtf8()}, {"Accept", "application/json"}}, this,
                      [this](bool ok, const QByteArray &, const QString &err) {
                          m_testResult->setText(ok ? tr("✔ API Key 可以使用") : tr("✘ 驗證失敗：%1").arg(err));
                      }, 0);
}

void SettingsDialog::save()
{
    Settings::i().setCurseforgeKey(m_key->text());
    const QString lang = m_lang->currentData().toString();
    m_langChanged = lang != Settings::i().language();
    Settings::i().setLanguage(lang);
    const bool registryChanged = m_registry->text().trimmed() != Settings::i().registryUrl();
    Settings::i().setRegistryUrl(m_registry->text());
    if (registryChanged)
        PeerService::i().restart();
    accept();
}

} // namespace MI
