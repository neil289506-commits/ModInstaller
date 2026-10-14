// SPDX-License-Identifier: GPL-3.0-only
#include "RequestToastHost.h"
#include "FileTargetDialog.h"
#include "core/Util.h"
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace MI {

class RequestCard : public QFrame
{
public:
    RequestCard(const IncomingRequest &r, QWidget *parent) : QFrame(parent), m_req(r)
    {
        setObjectName(QStringLiteral("requestCard"));
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(16, 12, 16, 14);
        lay->setSpacing(6);

        auto *head = new QLabel(QStringLiteral("Request"), this);
        head->setObjectName(QStringLiteral("requestHead"));
        m_text = new QLabel(QCoreApplication::translate("RequestCard", "<b>%1</b> 想傳送模組包給你").arg(r.fromCode.toHtmlEscaped()), this);
        m_text->setWordWrap(true);
        m_file = new QLabel(QStringLiteral("%1  ·  %2").arg(r.fileName.toHtmlEscaped(), humanSize(r.size)), this);
        m_file->setObjectName(QStringLiteral("muted"));
        m_file->setWordWrap(true);
        m_bar = new QProgressBar(this);
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(8);
        m_bar->setRange(0, 1000);
        m_bar->hide();

        auto *btns = new QHBoxLayout;
        m_reject = new QPushButton(QCoreApplication::translate("RequestCard", "拒絕"), this);
        m_reject->setObjectName(QStringLiteral("secondary"));
        m_accept = new QPushButton(QCoreApplication::translate("RequestCard", "接受"), this);
        m_open = new QPushButton(QCoreApplication::translate("RequestCard", "開啟資料夾"), this);
        m_open->setObjectName(QStringLiteral("secondary"));
        m_open->hide();
        btns->addStretch(1);
        btns->addWidget(m_reject);
        btns->addWidget(m_accept);
        btns->addWidget(m_open);

        lay->addWidget(head);
        lay->addWidget(m_text);
        lay->addWidget(m_file);
        lay->addWidget(m_bar);
        lay->addLayout(btns);
    }

    QPushButton *acceptBtn() const { return m_accept; }
    QPushButton *rejectBtn() const { return m_reject; }
    const IncomingRequest &request() const { return m_req; }

    void setReceiving()
    {
        m_accept->hide();
        m_reject->hide();
        m_bar->show();
        m_text->setText(QCoreApplication::translate("RequestCard", "正在接收 <b>%1</b> 的檔案…").arg(m_req.fromCode.toHtmlEscaped()));
    }
    void setProgress(qint64 d, qint64 t) { m_bar->setValue(int(t > 0 ? d * 1000 / t : 0)); }
    void setResult(bool ok, const QString &msg, const QString &path)
    {
        m_bar->hide();
        m_accept->hide();
        m_reject->hide();
        m_path = path;
        if (ok) {
            m_text->setText(QCoreApplication::translate("RequestCard", "<span style='color:#1bd96a'>✔ 已收到並通過驗證</span>"));
            m_file->setText(QDir::toNativeSeparators(path).toHtmlEscaped());
            m_open->show();
            connect(m_open, &QPushButton::clicked, this, [this]() {
#ifdef Q_OS_WIN
                QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(m_path)});
#else
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_path).absolutePath()));
#endif
            });
        } else {
            m_text->setText(QCoreApplication::translate("RequestCard", "<span style='color:#f0b429'>✘ 接收失敗</span>"));
            m_file->setText(msg.toHtmlEscaped());
        }
    }

private:
    IncomingRequest m_req;
    QLabel *m_text, *m_file;
    QProgressBar *m_bar;
    QPushButton *m_accept, *m_reject, *m_open;
    QString m_path;
};

RequestToastHost::RequestToastHost(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(350);
    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(0, 0, 0, 0);
    m_lay->setSpacing(10);
    parent->installEventFilter(this);
    auto &svc = PeerService::i();
    connect(&svc, &PeerService::incomingRequest, this, &RequestToastHost::onRequest);
    connect(&svc, &PeerService::requestExpired, this, &RequestToastHost::onExpired);
    connect(&svc, &PeerService::recvProgress, this, &RequestToastHost::onProgress);
    connect(&svc, &PeerService::recvFinished, this, &RequestToastHost::onFinished);
    hide();
}

bool RequestToastHost::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == parent() && e->type() == QEvent::Resize)
        reposition();
    return QWidget::eventFilter(obj, e);
}

void RequestToastHost::reposition()
{
    adjustSize();
    auto *p = parentWidget();
    // 左下角（避開底部資訊列）
    move(20, p->height() - height() - 56);
    raise();
}

void RequestToastHost::onRequest(const MI::IncomingRequest &r)
{
    auto *card = new RequestCard(r, this);
    m_cards.insert(r.id, card);
    m_lay->addWidget(card);
    connect(card->rejectBtn(), &QPushButton::clicked, this, [this, id = r.id]() {
        PeerService::i().reject(id);
        removeCard(id);
    });
    connect(card->acceptBtn(), &QPushButton::clicked, this, [this, id = r.id]() {
        auto it = m_cards.find(id);
        if (it == m_cards.end())
            return;
        const IncomingRequest &req = it.value()->request();
        const bool pak = QFileInfo(req.fileName).suffix().toLower() == QLatin1String("modpak");
        FileTargetDialog ft(tr("儲存收到的檔案"), pak ? FileTargetDialog::PakOnly : FileTargetDialog::ZipOnly, QFileInfo(req.fileName).completeBaseName(),
                            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                            tr("來自 %1 的檔案（%2）。請選擇儲存的名稱與位置。").arg(req.fromCode, humanSize(req.size)), window());
        if (ft.exec() != QDialog::Accepted)
            return; // 不儲存就維持提醒，使用者仍可拒絕
        it.value()->setReceiving();
        PeerService::i().accept(id, ft.filePath());
        reposition();
    });
    show();
    reposition();
    QApplication::alert(window(), 0); // 工作列閃爍提醒
}

void RequestToastHost::removeCard(const QString &id)
{
    auto card = m_cards.take(id);
    if (card) {
        m_lay->removeWidget(card);
        card->deleteLater();
    }
    QTimer::singleShot(0, this, [this]() {
        if (m_cards.isEmpty())
            hide();
        else
            reposition();
    });
}

void RequestToastHost::onExpired(const QString &id) { removeCard(id); }

void RequestToastHost::onProgress(const QString &id, qint64 d, qint64 t)
{
    if (auto *c = m_cards.value(id))
        c->setProgress(d, t);
}

void RequestToastHost::onFinished(const QString &id, bool ok, const QString &message)
{
    auto *c = m_cards.value(id);
    if (!c)
        return;
    c->setResult(ok, message, ok ? message : QString());
    reposition();
    QTimer::singleShot(ok ? 30000 : 12000, this, [this, id]() { removeCard(id); });
}

} // namespace MI
