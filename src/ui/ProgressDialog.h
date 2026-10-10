#pragma once
#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace MI {

class ProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProgressDialog(const QString &title, QWidget *parent = nullptr);
    void setStatus(const QString &text);
    void setProgress(int done, int total);
    void setBusy(); // 不確定進度

signals:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QLabel *m_label;
    QProgressBar *m_bar;
    QPushButton *m_cancel;
};

} // namespace MI
