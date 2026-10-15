#pragma once
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

namespace MI {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    bool languageChanged() const { return m_langChanged; }

private slots:
    void testKey();
    void save();

private:
    QLineEdit *m_key;
    QLineEdit *m_registry;
    QComboBox *m_lang;
    QLabel *m_testResult;
    bool m_langChanged = false;
};

} // namespace MI
