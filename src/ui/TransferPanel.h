// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFrame>

class QLabel;
class QLineEdit;
class QPushButton;

namespace MI {

// 主畫面右下角：顯示自己的 12 碼代碼，並可輸入對方代碼傳送模組包
class TransferPanel : public QFrame
{
    Q_OBJECT
public:
    explicit TransferPanel(QWidget *parent = nullptr);

private slots:
    void refresh();
    void copyCode();
    void sendClicked();

private:
    QLabel *m_code;
    QLabel *m_status;
    QPushButton *m_copy;
    QPushButton *m_send;
    QLineEdit *m_peer;
};

} // namespace MI
