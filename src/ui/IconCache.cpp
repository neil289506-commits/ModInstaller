#include "IconCache.h"
#include "core/Net.h"
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <webp/decode.h>

namespace MI {

IconCache &IconCache::i()
{
    static IconCache c;
    return c;
}

QPixmap IconCache::placeholder(const QString &name)
{
    // 以名稱首字母 + 固定配色當佔位圖，載入前 / 失敗時也好看
    static QHash<QString, QPixmap> cache;
    const QString key = name.trimmed().isEmpty() ? QString() : name.trimmed().left(1).toUpper();
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    static const QColor palette[] = {QColor(0xe8, 0x59, 0x0c), QColor(0x2f, 0x9e, 0x44), QColor(0x70, 0x48, 0xe8), QColor(0x19, 0x71, 0xc2),
                                     QColor(0xd6, 0x33, 0x6c), QColor(0x0c, 0x85, 0x99), QColor(0xf0, 0x8c, 0x00), QColor(0x86, 0x8e, 0x96)};
    QPixmap p(64, 64);
    p.fill(Qt::transparent);
    QPainter pa(&p);
    pa.setRenderHint(QPainter::Antialiasing);
    pa.setPen(Qt::NoPen);
    if (key.isEmpty()) {
        pa.setBrush(QColor(0x2f, 0x33, 0x3a));
        pa.drawRoundedRect(QRectF(0, 0, 64, 64), 10, 10);
        pa.setPen(QPen(QColor(0x5c, 0x65, 0x73), 3));
        pa.setBrush(Qt::NoBrush);
        pa.drawRoundedRect(QRectF(18, 18, 28, 28), 4, 4);
    } else {
        pa.setBrush(palette[key.at(0).unicode() % 8]);
        pa.drawRoundedRect(QRectF(0, 0, 64, 64), 10, 10);
        QFont f = pa.font();
        f.setPixelSize(34);
        f.setBold(true);
        pa.setFont(f);
        pa.setPen(Qt::white);
        pa.drawText(QRectF(0, 0, 64, 64), Qt::AlignCenter, key);
    }
    cache.insert(key, p);
    return p;
}

QPixmap IconCache::decode(const QByteArray &data)
{
    QPixmap px;
    if (px.loadFromData(data))
        return px;
    // WebP：RIFF....WEBP
    if (data.size() > 12 && data.startsWith("RIFF") && data.mid(8, 4) == "WEBP") {
        int w = 0, h = 0;
        if (WebPGetInfo(reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()), &w, &h) && w > 0 && h > 0) {
            uint8_t *rgba = WebPDecodeRGBA(reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()), &w, &h);
            if (rgba) {
                QImage img(rgba, w, h, w * 4, QImage::Format_RGBA8888);
                px = QPixmap::fromImage(img.copy()); // copy：脫離 libwebp 配置的記憶體
                WebPFree(rgba);
            }
        }
    }
    return px;
}

QPixmap IconCache::get(const QString &url, const QString &name)
{
    if (url.isEmpty())
        return placeholder(name);
    auto it = m_cache.constFind(url);
    if (it != m_cache.constEnd())
        return it.value().isNull() ? placeholder(name) : it.value();
    if (!m_pending.contains(url)) {
        m_pending.insert(url);
        Net::i().getBytes(QUrl(url), {}, this, [this, url](bool ok, const QByteArray &d, const QString &) {
            m_pending.remove(url);
            QPixmap src = ok ? decode(d) : QPixmap();
            if (!src.isNull()) {
                QPixmap out(64, 64);
                out.fill(Qt::transparent);
                QPainter p(&out);
                p.setRenderHint(QPainter::Antialiasing);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                QPainterPath path;
                path.addRoundedRect(QRectF(0, 0, 64, 64), 10, 10);
                p.setClipPath(path);
                p.drawPixmap(0, 0, src.scaled(64, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                m_cache.insert(url, out);
            } else {
                m_cache.insert(url, QPixmap()); // 失敗：之後都用佔位圖
                qWarning("IconCache: 無法載入圖示 %s", qPrintable(url));
            }
            emit loaded(url);
        }, 1);
    }
    return placeholder(name);
}

} // namespace MI
