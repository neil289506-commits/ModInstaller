#include "LoaderVersionDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace MI {

LoaderVersionDialog::LoaderVersionDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("建立模組包"));
    setModal(true);
    setMinimumWidth(500);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    m_data = new VersionData(this);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(10);
    auto *head = new QLabel(tr("選擇模組載入器與版本"), this);
    head->setObjectName(QStringLiteral("heading"));
    lay->addWidget(head);

    auto addField = [&](const QString &label, QWidget *w) {
        auto *l = new QLabel(label, this);
        l->setObjectName(QStringLiteral("fieldLabel"));
        lay->addWidget(l);
        lay->addWidget(w);
    };

    m_loader = new QComboBox(this);
    for (Loader l : {Loader::Fabric, Loader::Quilt, Loader::Forge, Loader::NeoForge})
        m_loader->addItem(loaderName(l), int(l));
    m_mc = new QComboBox(this);
    m_snap = new QCheckBox(tr("顯示快照版本"), this);
    m_lv = new QComboBox(this);
    addField(tr("模組載入器"), m_loader);
    addField(tr("遊戲版本"), m_mc);
    lay->addWidget(m_snap);
    addField(tr("載入器版本"), m_lv);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("muted"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    auto *btns = new QHBoxLayout;
    m_refresh = new QPushButton(tr("更新版本資料"), this);
    m_refresh->setObjectName(QStringLiteral("secondary"));
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    m_ok = new QPushButton(tr("下一步"), this);
    m_ok->setDefault(true);
    btns->addWidget(m_refresh);
    btns->addStretch(1);
    btns->addWidget(cancel);
    btns->addWidget(m_ok);
    lay->addSpacing(8);
    lay->addLayout(btns);

    connect(m_loader, &QComboBox::currentIndexChanged, this, &LoaderVersionDialog::onLoaderChanged);
    connect(m_snap, &QCheckBox::toggled, this, &LoaderVersionDialog::refillMc);
    connect(m_mc, &QComboBox::currentIndexChanged, this, &LoaderVersionDialog::refillLoaderVersions);
    connect(m_refresh, &QPushButton::clicked, this, [this]() {
        setBusy(true, tr("正在下載版本資料…"));
        m_data->fetchAll();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_ok, &QPushButton::clicked, this, [this]() {
        if (m_mc->currentText().isEmpty() || m_lv->currentText().isEmpty()) {
            QMessageBox::information(this, windowTitle(), tr("請選擇遊戲版本與載入器版本"));
            return;
        }
        accept();
    });
    connect(m_data, &VersionData::progress, this, [this](int done, int total, const QString &name) {
        m_status->setText(tr("正在下載版本資料… (%1/%2) %3").arg(done + 1).arg(total).arg(name));
    });
    connect(m_data, &VersionData::finished, this, [this](bool ok, const QString &err) {
        setBusy(false);
        if (!ok) {
            m_status->setText(err);
            return;
        }
        m_status->setText(err);
        refillMc();
    });

    QTimer::singleShot(0, this, &LoaderVersionDialog::loadData);
}

void LoaderVersionDialog::setBusy(bool busy, const QString &text)
{
    m_loader->setEnabled(!busy);
    m_mc->setEnabled(!busy);
    m_lv->setEnabled(!busy);
    m_snap->setEnabled(!busy);
    m_refresh->setEnabled(!busy);
    m_ok->setEnabled(!busy);
    if (!text.isEmpty())
        m_status->setText(text);
}

void LoaderVersionDialog::loadData()
{
    QString err;
    if (m_data->hasLocal() && m_data->load(&err)) {
        refillMc();
        m_status->setText(tr("已載入本機版本資料，可按「更新版本資料」取得最新版本。"));
    } else {
        setBusy(true, tr("正在下載版本資料…"));
        m_data->fetchAll();
    }
}

void LoaderVersionDialog::onLoaderChanged() { refillMc(); }

void LoaderVersionDialog::refillMc()
{
    const Loader l = Loader(m_loader->currentData().toInt());
    const QString prev = m_mc->currentText();
    m_mc->blockSignals(true);
    m_mc->clear();
    m_mc->addItems(m_data->mcVersions(l, m_snap->isChecked()));
    const int i = m_mc->findText(prev);
    if (i >= 0)
        m_mc->setCurrentIndex(i);
    m_mc->blockSignals(false);
    refillLoaderVersions();
}

void LoaderVersionDialog::refillLoaderVersions()
{
    const Loader l = Loader(m_loader->currentData().toInt());
    m_lv->clear();
    const auto list = m_data->loaderVersions(l, m_mc->currentText());
    int pick = -1;
    for (int i = 0; i < list.size(); ++i) {
        const auto &lv = list[i];
        QString label = lv.version;
        if (lv.recommended)
            label += tr("（推薦）");
        else if (!lv.stable)
            label += tr("（測試版）");
        m_lv->addItem(label, lv.version);
        if (pick < 0 && (lv.recommended || (lv.stable && (l == Loader::Fabric || l == Loader::Quilt))))
            pick = i;
    }
    if (pick < 0 && !list.isEmpty())
        pick = 0;
    if (pick >= 0)
        m_lv->setCurrentIndex(pick);
}

PackTarget LoaderVersionDialog::target() const
{
    PackTarget t;
    t.loader = Loader(m_loader->currentData().toInt());
    t.mcVersion = m_mc->currentText();
    t.loaderVersion = m_lv->currentData().toString();
    return t;
}

} // namespace MI
