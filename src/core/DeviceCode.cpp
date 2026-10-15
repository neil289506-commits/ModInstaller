// SPDX-License-Identifier: GPL-3.0-only
#include "DeviceCode.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace MI {

namespace {
const char *kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

QSettings &store()
{
    // 登錄檔（Windows: HKEY_CURRENT_USER\Software\ModInstaller\Device）
    static QSettings s(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("ModInstaller"), QStringLiteral("Device"));
    return s;
}
} // namespace

QString DeviceCode::filePath()
{
    // %LOCALAPPDATA%/ModInstaller/ModInstaller/device.json
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/device.json");
}

QString DeviceCode::generate()
{
    QString code;
    code.reserve(Length);
    const int n = int(qstrlen(kAlphabet));
    for (int i = 0; i < Length; ++i)
        code += QLatin1Char(kAlphabet[QRandomGenerator::system()->bounded(n)]);
    return code;
}

QString DeviceCode::normalize(const QString &input)
{
    QString s = input.toUpper();
    s.remove(QRegularExpression(QStringLiteral("[\\s\\-]")));
    return s;
}

bool DeviceCode::isValid(const QString &code)
{
    static const QRegularExpression re(QStringLiteral("^[A-Z0-9]{12}$"));
    return re.match(code).hasMatch();
}

DeviceCode::Record DeviceCode::load()
{
    Record fromReg;
    fromReg.code = normalize(store().value(QStringLiteral("code")).toString());
    fromReg.registered = store().value(QStringLiteral("registered")).toBool();

    Record fromFile;
    QFile f(filePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        fromFile.code = normalize(o.value(QStringLiteral("code")).toString());
        fromFile.registered = o.value(QStringLiteral("registered")).toBool();
    }

    Record r;
    if (isValid(fromReg.code) && isValid(fromFile.code) && fromReg.code != fromFile.code) {
        // 兩處不一致（極少見）：以已註冊的為準，否則以登錄檔為準
        r = (fromFile.registered && !fromReg.registered) ? fromFile : fromReg;
    } else if (isValid(fromReg.code)) {
        r = fromReg;
        r.registered = fromReg.registered || (fromFile.code == fromReg.code && fromFile.registered);
    } else if (isValid(fromFile.code)) {
        r = fromFile;
    }
    if (isValid(r.code) && (r.code != fromReg.code || r.code != fromFile.code || r.registered != fromReg.registered ||
                            r.registered != fromFile.registered))
        save(r); // 某一處遺失或過期 → 由另一處還原
    return r;
}

void DeviceCode::save(const Record &r)
{
    store().setValue(QStringLiteral("code"), r.code);
    store().setValue(QStringLiteral("registered"), r.registered);
    store().sync();

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject o;
        o[QStringLiteral("code")] = r.code;
        o[QStringLiteral("registered")] = r.registered;
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    }
}

} // namespace MI
