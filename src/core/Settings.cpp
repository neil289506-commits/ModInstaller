#include "Settings.h"
#include <QDir>
#include <QLocale>
#include <QStandardPaths>

namespace MI {

Settings &Settings::i()
{
    static Settings s;
    return s;
}

Settings::Settings()
    : m_s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("ModInstaller"), QStringLiteral("ModInstaller"))
{
}

QString Settings::curseforgeKey() const { return m_s.value(QStringLiteral("curseforge/apiKey")).toString().trimmed(); }
void Settings::setCurseforgeKey(const QString &k) { m_s.setValue(QStringLiteral("curseforge/apiKey"), k.trimmed()); }

QString Settings::language() const
{
    const QString v = m_s.value(QStringLiteral("ui/language")).toString();
    if (!v.isEmpty())
        return v;
    return QLocale::system().name().startsWith(QLatin1String("zh")) ? QStringLiteral("zh_TW") : QStringLiteral("en");
}
void Settings::setLanguage(const QString &l) { m_s.setValue(QStringLiteral("ui/language"), l); }

QString Settings::lastDir(const QString &key) const
{
    const QString d = m_s.value(QStringLiteral("dirs/") + key).toString();
    if (!d.isEmpty() && QDir(d).exists())
        return d;
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}
void Settings::setLastDir(const QString &key, const QString &dir) { m_s.setValue(QStringLiteral("dirs/") + key, dir); }

QString Settings::versionDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/version");
}

} // namespace MI
