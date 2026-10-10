#pragma once
#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace MI {

// "1.20.4" → [1,20,4]；非數字尾綴忽略
inline QVector<int> semVec(const QString &ver)
{
    QVector<int> v;
    for (const QString &part : ver.split(QLatin1Char('.'))) {
        QString num;
        for (QChar c : part) {
            if (c.isDigit())
                num += c;
            else
                break;
        }
        v.append(num.isEmpty() ? 0 : num.toInt());
    }
    return v;
}

// a 是否比 b 新
inline bool semGt(const QString &a, const QString &b)
{
    const auto va = semVec(a);
    const auto vb = semVec(b);
    const int n = qMax(va.size(), vb.size());
    for (int i = 0; i < n; ++i) {
        const int ai = i < va.size() ? va[i] : 0;
        const int bi = i < vb.size() ? vb[i] : 0;
        if (ai != bi)
            return ai > bi;
    }
    return false;
}

// 同 Prism GetModDependenciesTask 的 laxCompare
inline bool laxCompare(const QString &fsFilename, const QString &metaFilename, bool excludeDigits = false)
{
    QString a = fsFilename.toLower();
    QString b = metaFilename.toLower();
    QString seps = QStringLiteral("-+._");
    if (excludeDigits)
        seps += QStringLiteral("0123456789");
    for (QChar s : seps) {
        a.replace(s, QLatin1Char(' '));
        b.replace(s, QLatin1Char(' '));
    }
    return a.simplified() == b.simplified();
}

inline QString sanitizeFileName(QString name)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|]"));
    name.replace(bad, QStringLiteral("_"));
    return name.trimmed();
}

inline QString humanSize(qint64 b)
{
    const char *u[] = {"B", "KB", "MB", "GB"};
    double v = double(b);
    int i = 0;
    while (v >= 1024.0 && i < 3) {
        v /= 1024.0;
        ++i;
    }
    return QStringLiteral("%1 %2").arg(v, 0, 'f', i ? 1 : 0).arg(QLatin1String(u[i]));
}

inline QString humanCount(qint64 n)
{
    if (n >= 1000000000)
        return QStringLiteral("%1B").arg(n / 1e9, 0, 'f', 1);
    if (n >= 1000000)
        return QStringLiteral("%1M").arg(n / 1e6, 0, 'f', 1);
    if (n >= 1000)
        return QStringLiteral("%1K").arg(n / 1e3, 0, 'f', 1);
    return QString::number(n);
}

} // namespace MI
