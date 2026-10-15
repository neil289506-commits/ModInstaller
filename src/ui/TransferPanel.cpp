// SPDX-License-Identifier: GPL-3.0-only
#include "TransferPanel.h"
#include "ProgressDialog.h"
#include "core/PeerService.h"
#include "core/Settings.h"
#include "core/Util.h"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace MI {

static QString groupedCode(const QString &c)
{
    if (c.size() != DeviceCode::Length)
        return c.isEmpty() ? QStringLiteral("············") : c;
    return c.left(4) + QLatin1Char(' ') + c.mid(4, 4) + QLatin1Char(' ') + c.mid(8, 4);
}

TransferPanel::TransferPanel(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("transferPanel"));
    setFixedWidth(356);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 12, 16, 14);
    lay->setSpacing(8);

    auto *top = new QHBoxLayout;
    auto *cap = new QLabel(tr("我的代碼"), this);
    cap->setObjectName(QStringLiteral("muted"));
    m_code = new QLabel(this);
    m_code->setObjectName(QStringLiteral("deviceCode"));
    m_code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_copy = new QPushButton(tr("複製"), this);
    m_copy->setObjectName(QStringLiteral("secondary"));
    m_copy->setCursor(Qt::PointingHandCursor);
    top->addWidget(cap);
    top->addWidget(m_code, 1);
    top->addWidget(m_copy);
    lay->addLayout(top);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("muted"));
    lay->addWidget(m_status);

    auto *row = new QHBoxLayout;
    m_peer = new QLineEdit(this);
    m_peer->setPlaceholderText(tr("輸入對方的 12 碼代碼"));
    m_peer->setMaxLength(16);
    m_send = new QPushButton(tr("傳送"), this);
    row->addWidget(m_peer, 1);
    row->addWidget(m_send);
    lay->addLayout(row);

    auto &svc = PeerService::i();
    connect(&svc, &PeerService::stateChanged, this, &TransferPanel::refresh);
    connect(&svc, &PeerService::codeChanged, this, &TransferPanel::refresh);
    connect(m_copy, &QPushButton::clicked, this, &TransferPanel::copyCode);
    connect(m_send, &QPushButton::clicked, this, &TransferPanel::sendClicked);
    connect(m_peer, &QLineEdit::returnPressed, this, &TransferPanel::sendClicked);
    refresh();
}

void TransferPanel::refresh()
{
    auto &svc = PeerService::i();
    m_code->setText(groupedCode(svc.code()));
    const bool online = svc.isOnline();
    const QString color = online ? QStringLiteral("#1bd96a") : (svc.state() == PeerService::State::Error ? QStringLiteral("#f0b429") : QStringLiteral("#8a94a3"));
    QString text = svc.stateText();
    if (online && !svc.isRegistered())
        text += tr("（未向雲端註冊）");
    m_status->setText(QStringLiteral("<span style='color:%1'>●</span> %2").arg(color, text.toHtmlEscaped()));
    m_copy->setEnabled(!svc.code().isEmpty());
    m_send->setEnabled(online);
}

void TransferPanel::copyCode()
{
    QApplication::clipboard()->setText(PeerService::i().code());
    m_copy->setText(tr("已複製"));
    QTimer::singleShot(1500, this, [this]() { m_copy->setText(tr("複製")); });
}

static QString reasonText(const QString &reason)
{
    if (reason == QLatin1String("rejected")) return QCoreApplication::translate("TransferPanel", "對方拒絕了這次傳輸");
    if (reason == QLatin1String("offline")) return QCoreApplication::translate("TransferPanel", "對方目前不在線上。請確認對方已開啟程式，且右下角顯示「已連線」。");
    if (reason == QLatin1String("timeout")) return QCoreApplication::translate("TransferPanel", "對方沒有回應（逾時）");
    if (reason == QLatin1String("busy")) return QCoreApplication::translate("TransferPanel", "對方有太多待處理的請求，請稍後再試");
    if (reason == QLatin1String("unsupported")) return QCoreApplication::translate("TransferPanel", "對方無法接收這個檔案");
    if (reason.startsWith(QLatin1String("failed:"))) return QCoreApplication::translate("TransferPanel", "傳輸失敗：%1").arg(reason.mid(7));
    return reason;
}

void TransferPanel::sendClicked()
{
    auto &svc = PeerService::i();
    const QString peer = DeviceCode::normalize(m_peer->text());
    if (!DeviceCode::isValid(peer)) {
        QMessageBox::information(this, tr("傳送模組包"), tr("請輸入對方的 12 碼英數代碼。"));
        return;
    }
    if (peer == svc.code()) {
        QMessageBox::information(this, tr("傳送模組包"), tr("這是你自己的代碼，請輸入另一台電腦的代碼。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(window(), tr("選擇要傳送的模組包"), Settings::i().lastDir(QStringLiteral("pak")),
                                                      tr("模組包 (*.modpak *.zip);;所有檔案 (*)"));
    if (path.isEmpty())
        return;
    Settings::i().setLastDir(QStringLiteral("pak"), QFileInfo(path).absolutePath());

    // 先向雲端確認代碼存在（查不到狀態時不阻擋）
    bool proceed = false;
    bool known = false, exists = true;
    {
        ProgressDialog wait(tr("傳送模組包"), window());
        wait.setStatus(tr("正在確認對方代碼…"));
        wait.setBusy();
        connect(&wait, &ProgressDialog::cancelRequested, &wait, &QDialog::reject);
        svc.checkCodeExists(peer, &wait, [&](bool k, bool e) {
            known = k;
            exists = e;
            wait.accept();
        });
        proceed = wait.exec() == QDialog::Accepted;
    }
    if (!proceed)
        return;
    if (known && !exists) {
        QMessageBox::warning(this, tr("傳送模組包"), tr("找不到這組代碼，請確認輸入是否正確。"));
        return;
    }

    QString err;
    const QString id = svc.sendFile(peer, path, &err);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("傳送模組包"), err);
        return;
    }

    ProgressDialog dlg(tr("傳送模組包"), window());
    dlg.setStatus(tr("等待對方接受…"));
    dlg.setBusy();
    bool ok = false;
    QString reason;
    connect(&svc, &PeerService::sendStatus, &dlg, [&](const QString &i, const QString &t) {
        if (i == id)
            dlg.setStatus(t);
    });
    connect(&svc, &PeerService::sendProgress, &dlg, [&](const QString &i, qint64 d, qint64 t) {
        if (i != id)
            return;
        dlg.setProgress(int(t > 0 ? d * 1000 / t : 0), 1000);
        dlg.setStatus(tr("傳送中… %1 / %2").arg(humanSize(d), humanSize(t)));
    });
    connect(&svc, &PeerService::sendFinished, &dlg, [&](const QString &i, bool o, const QString &r) {
        if (i != id)
            return;
        ok = o;
        reason = r;
        dlg.accept();
    });
    connect(&dlg, &ProgressDialog::cancelRequested, &svc, [&]() { svc.cancel(id); });
    dlg.exec();

    if (ok)
        QMessageBox::information(this, tr("傳送模組包"), tr("傳送完成，對方已收到並通過驗證。"));
    else if (reason != QLatin1String("cancelled"))
        QMessageBox::warning(this, tr("傳送模組包"), reasonText(reason));
}

} // namespace MI
