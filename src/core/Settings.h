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
    // 實際使用的金鑰：使用者在設定填的優先，沒有則用建置時內建的
    QString effectiveCurseforgeKey() const;
    bool hasEmbeddedCurseforgeKey() const;

    // Cloudflare 註冊服務網址（用於確認 12 碼代碼不重複）
    QString registryUrl() const;
    void setRegistryUrl(const QString &u);

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
