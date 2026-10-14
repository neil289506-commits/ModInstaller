// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/PeerService.h"
#include <QHash>
#include <QWidget>

class QVBoxLayout;

namespace MI {

class RequestCard;

// 主畫面左下角：收到傳輸請求（Request）時的提醒卡片，可選擇接受或拒絕
class RequestToastHost : public QWidget
{
    Q_OBJECT
public:
    explicit RequestToastHost(QWidget *parent);
    void reposition();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
    void onRequest(const MI::IncomingRequest &r);
    void onExpired(const QString &id);
    void onProgress(const QString &id, qint64 done, qint64 total);
    void onFinished(const QString &id, bool ok, const QString &message);

private:
    void removeCard(const QString &id);
    QVBoxLayout *m_lay;
    QHash<QString, RequestCard *> m_cards;
};

} // namespace MI
