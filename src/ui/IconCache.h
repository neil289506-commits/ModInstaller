#pragma once
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <functional>

namespace MI {

// 模組圖示快取：非同步下載、縮放後快取
class IconCache : public QObject
{
    Q_OBJECT
public:
    static IconCache &i();
    // 取得圖示；尚未載入時回傳預設圖並於載入完成後發出 loaded
    QPixmap get(const QString &url, const QString &name = QString());
    static QPixmap placeholder(const QString &name = QString());
    // 解碼圖片（Qt 內建格式 + 內建 libwebp）
    static QPixmap decode(const QByteArray &data);

signals:
    void loaded(const QString &url);

private:
    IconCache() = default;
    QHash<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
};

} // namespace MI
