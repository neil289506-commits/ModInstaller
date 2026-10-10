#pragma once
#include <QSettings>
#include <QString>

namespace MI {

class Settings
{
public:
    static Settings &i();

    QString curseforgeKey() const;
    void setCurseforgeKey(const QString &k);

    QString language() const; // "zh_TW" / "en"
    void setLanguage(const QString &l);

    QString lastDir(const QString &key) const;
    void setLastDir(const QString &key, const QString &dir);

    // 版本資料（version/）的存放資料夾
    QString versionDir() const;

private:
    Settings();
    QSettings m_s;
};

} // namespace MI
