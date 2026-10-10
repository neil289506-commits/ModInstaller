#include "BlockedModsDialog.h"
#include "core/Settings.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace MI {

BlockedModsDialog::BlockedModsDialog(const QList<JobMod> &mods, const QList<int> &indices, QWidget *parent)
    : QDialog(parent), m_mods(mods), m_indices(indices)
{
    setWindowTitle(tr("需要手動下載的模組"));
    setModal(true);
    resize(760, 460);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(12);
    auto *head = new QLabel(tr("以下模組的作者禁止第三方程式下載"), this);
    head->setObjectName(QStringLiteral("heading"));
    auto *info = new QLabel(tr("請點「開啟頁面」用瀏覽器下載檔案，再於下方資料夾自動偵測；也可以手動選擇檔案。未提供的模組會被略過。"), this);
    info->setWordWrap(true);
    info->setObjectName(QStringLiteral("muted"));
    lay->addWidget(head);
    lay->addWidget(info);

    auto *dirRow = new QHBoxLayout;
    m_dir = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), this);
    auto *bd = new QPushButton(tr("瀏覽…"), this);
    bd->setObjectName(QStringLiteral("secondary"));
    dirRow->addWidget(new QLabel(tr("偵測資料夾"), this));
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(bd);
    lay->addLayout(dirRow);

    m_table = new QTableWidget(m_indices.size(), 4, this);
    m_table->setHorizontalHeaderLabels({tr("模組"), tr("檔案名稱"), tr("狀態"), QString()});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    lay->addWidget(m_table, 1);

    for (int row = 0; row < m_indices.size(); ++row) {
        const JobMod &m = m_mods[m_indices[row]];
        m_table->setItem(row, 0, new QTableWidgetItem(m.name));
        m_table->setItem(row, 1, new QTableWidgetItem(m.fileName));
        m_table->setItem(row, 2, new QTableWidgetItem(tr("等待下載")));
        auto *w = new QWidget(m_table);
        auto *hl = new QHBoxLayout(w);
        hl->setContentsMargins(4, 2, 4, 2);
        auto *open = new QPushButton(tr("開啟頁面"), w);
        auto *pick = new QPushButton(tr("選擇檔案"), w);
        open->setObjectName(QStringLiteral("secondary"));
        pick->setObjectName(QStringLiteral("secondary"));
        hl->addWidget(open);
        hl->addWidget(pick);
        m_table->setCellWidget(row, 3, w);
        connect(open, &QPushButton::clicked, this, [m]() {
            QString url = m.websiteUrl;
            if (!url.isEmpty())
                url += QStringLiteral("/files/") + m.versionId;
            if (!url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
        });
        connect(pick, &QPushButton::clicked, this, [this, row]() {
            const QString f = QFileDialog::getOpenFileName(this, tr("選擇已下載的檔案"), m_dir->text(), tr("模組檔案 (*.jar);;所有檔案 (*)"));
            if (!f.isEmpty()) {
                m_found[m_indices[row]] = f;
                updateRow(row);
            }
        });
    }

    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    auto *ok = new QPushButton(tr("繼續"), this);
    ok->setDefault(true);
    btns->addWidget(cancel);
    btns->addWidget(ok);
    lay->addLayout(btns);

    connect(bd, &QPushButton::clicked, this, &BlockedModsDialog::browseDir);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, [this]() {
        if (m_found.size() < m_indices.size()) {
            const auto r = QMessageBox::question(this, windowTitle(), tr("還有 %1 個模組尚未提供，將被略過。確定繼續嗎？").arg(m_indices.size() - m_found.size()));
            if (r != QMessageBox::Yes)
                return;
        }
        accept();
    });

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &BlockedModsDialog::scan);
    m_timer->start(1000);
}

void BlockedModsDialog::browseDir()
{
    const QString d = QFileDialog::getExistingDirectory(this, tr("選擇偵測資料夾"), m_dir->text());
    if (!d.isEmpty())
        m_dir->setText(d);
}

void BlockedModsDialog::updateRow(int row)
{
    const int idx = m_indices[row];
    m_table->item(row, 2)->setText(m_found.contains(idx) ? tr("已取得") : tr("等待下載"));
}

void BlockedModsDialog::scan()
{
    const QDir d(m_dir->text());
    if (!d.exists())
        return;
    for (int row = 0; row < m_indices.size(); ++row) {
        const int idx = m_indices[row];
        if (m_found.contains(idx))
            continue;
        const QString name = m_mods[idx].fileName;
        if (d.exists(name)) {
            m_found[idx] = d.absoluteFilePath(name);
            updateRow(row);
        }
    }
}

} // namespace MI
