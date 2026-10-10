#include "IconCache.h"
#include "core/Net.h"
#include <QPainter>
#include <QPainterPath>

namespace MI {

IconCache &IconCache::i()
{
    static IconCache c;
    return c;
}

QPixmap IconCache::placeholder()
{
    static QPixmap p;
    if (p.isNull()) {
        p = QPixmap(64, 64);
        p.fill(Qt::transparent);
        QPainter pa(&p);
        pa.setRenderHint(QPainter::Antialiasing);
        pa.setBrush(QColor(0x2f, 0x33, 0x3a));
        pa.setPen(Qt::NoPen);
        pa.drawRoundedRect(QRectF(0, 0, 64, 64), 10, 10);
        pa.setPen(QPen(QColor(0x5c, 0x65, 0x73), 3));
        pa.drawRoundedRect(QRectF(18, 18, 28, 28), 4, 4);
    }
    return p;
}

QPixmap IconCache::get(const QString &url)
{
    if (url.isEmpty())
        return placeholder();
    auto it = m_cache.constFind(url);
    if (it != m_cache.constEnd())
        return it.value();
    if (!m_pending.contains(url)) {
        m_pending.insert(url);
        Net::i().getBytes(QUrl(url), {}, this, [this, url](bool ok, const QByteArray &d, const QString &) {
            m_pending.remove(url);
            QPixmap src;
            if (ok && src.loadFromData(d)) {
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
                m_cache.insert(url, placeholder());
            }
            emit loaded(url);
        }, 1);
    }
    return placeholder();
}

} // namespace MI
