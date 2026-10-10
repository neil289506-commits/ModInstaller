#include "FileTargetDialog.h"
#include "core/Settings.h"
#include "core/Util.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace MI {

FileTargetDialog::FileTargetDialog(const QString &title, Kind kind, const QString &defaultName, const QString &defaultDir,
                                   const QString &info, QWidget *parent)
    : QDialog(parent), m_kind(kind)
{
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(520);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(12);

    auto *head = new QLabel(title, this);
    head->setObjectName(QStringLiteral("heading"));
    lay->addWidget(head);

    if (!info.isEmpty()) {
        auto *il = new QLabel(info, this);
        il->setWordWrap(true);
        il->setObjectName(QStringLiteral("muted"));
        lay->addWidget(il);
    }

    if (kind == ChooseFormat) {
        auto *fl = new QLabel(tr("輸出格式"), this);
        fl->setObjectName(QStringLiteral("fieldLabel"));
        lay->addWidget(fl);
        m_zip = new QRadioButton(tr("打包成 zip（下載所有模組並壓縮，解壓即可使用）"), this);
        m_pak = new QRadioButton(tr("打包成 modpak（只儲存模組資訊，檔案很小，之後可再請求下載）"), this);
        m_zip->setChecked(true);
        lay->addWidget(m_zip);
        lay->addWidget(m_pak);
    }

    auto *nl = new QLabel(tr("檔案名稱"), this);
    nl->setObjectName(QStringLiteral("fieldLabel"));
    m_name = new QLineEdit(defaultName, this);
    lay->addWidget(nl);
    lay->addWidget(m_name);

    auto *dl = new QLabel(tr("儲存位置"), this);
    dl->setObjectName(QStringLiteral("fieldLabel"));
    m_dir = new QLineEdit(defaultDir.isEmpty() ? Settings::i().lastDir(QStringLiteral("output")) : defaultDir, this);
    auto *browse = new QPushButton(tr("瀏覽…"), this);
    browse->setObjectName(QStringLiteral("secondary"));
    auto *row = new QHBoxLayout;
    row->addWidget(m_dir, 1);
    row->addWidget(browse);
    lay->addWidget(dl);
    lay->addLayout(row);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("muted"));
    m_preview->setWordWrap(true);
    lay->addWidget(m_preview);

    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    m_ok = new QPushButton(tr("確定"), this);
    m_ok->setDefault(true);
    btns->addWidget(cancel);
    btns->addWidget(m_ok);
    lay->addLayout(btns);

    connect(browse, &QPushButton::clicked, this, &FileTargetDialog::browse);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_ok, &QPushButton::clicked, this, &FileTargetDialog::tryAccept);
    connect(m_name, &QLineEdit::textChanged, this, &FileTargetDialog::refreshPreview);
    connect(m_dir, &QLineEdit::textChanged, this, &FileTargetDialog::refreshPreview);
    if (m_zip) {
        connect(m_zip, &QRadioButton::toggled, this, &FileTargetDialog::refreshPreview);
    }
    refreshPreview();
}

bool FileTargetDialog::isPak() const
{
    if (m_kind == PakOnly)
        return true;
    if (m_kind == ZipOnly)
        return false;
    return m_pak->isChecked();
}

QString FileTargetDialog::baseName() const
{
    QString n = sanitizeFileName(m_name->text());
    for (const QString ext : {QStringLiteral(".zip"), QStringLiteral(".modpak")})
        if (n.endsWith(ext, Qt::CaseInsensitive))
            n.chop(ext.size());
    return n;
}

QString FileTargetDialog::filePath() const
{
    return QDir(m_dir->text().trimmed()).absoluteFilePath(baseName() + (isPak() ? QStringLiteral(".modpak") : QStringLiteral(".zip")));
}

void FileTargetDialog::refreshPreview()
{
    m_preview->setText(tr("將輸出到：%1").arg(QDir::toNativeSeparators(filePath())));
    m_ok->setEnabled(!baseName().isEmpty() && !m_dir->text().trimmed().isEmpty());
}

void FileTargetDialog::browse()
{
    const QString d = QFileDialog::getExistingDirectory(this, tr("選擇儲存位置"), m_dir->text());
    if (!d.isEmpty())
        m_dir->setText(d);
}

void FileTargetDialog::tryAccept()
{
    const QDir dir(m_dir->text().trimmed());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        QMessageBox::warning(this, windowTitle(), tr("無法建立資料夾：%1").arg(dir.absolutePath()));
        return;
    }
    if (QFileInfo::exists(filePath())) {
        const auto r = QMessageBox::question(this, windowTitle(), tr("檔案已存在，要覆寫嗎？\n%1").arg(QDir::toNativeSeparators(filePath())));
        if (r != QMessageBox::Yes)
            return;
    }
    Settings::i().setLastDir(QStringLiteral("output"), dir.absolutePath());
    accept();
}

} // namespace MI
