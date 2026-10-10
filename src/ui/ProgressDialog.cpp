#include "ProgressDialog.h"
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace MI {

ProgressDialog::ProgressDialog(const QString &title, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(460);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(14);
    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(10);
    m_cancel = new QPushButton(tr("取消"), this);
    m_cancel->setObjectName(QStringLiteral("secondary"));
    lay->addWidget(m_label);
    lay->addWidget(m_bar);
    lay->addWidget(m_cancel, 0, Qt::AlignRight);
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        m_cancel->setEnabled(false);
        emit cancelRequested();
    });
}

void ProgressDialog::setStatus(const QString &text) { m_label->setText(text); }

void ProgressDialog::setProgress(int done, int total)
{
    m_bar->setRange(0, qMax(1, total));
    m_bar->setValue(done);
}

void ProgressDialog::setBusy() { m_bar->setRange(0, 0); }

void ProgressDialog::closeEvent(QCloseEvent *e)
{
    e->ignore();
    m_cancel->click();
}

void ProgressDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        e->ignore();
        return;
    }
    QDialog::keyPressEvent(e);
}

} // namespace MI
