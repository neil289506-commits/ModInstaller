#pragma once
#include "Models.h"
#include <QMap>
#include <QObject>
#include <QSet>

namespace MI {

struct LoaderVersion {
    QString mc;
    QString version;
    bool stable = true;
    bool recommended = false;
};

struct VanillaVersion {
    QString id;
    QString type; // release / snapshot / old_*
};

// 版本資料：下載 version/ 內的各來源檔案並解析（取代原本的 version.py，不需要 Python）
class VersionData : public QObject
{
    Q_OBJECT
public:
    explicit VersionData(QObject *parent = nullptr);

    bool hasLocal() const;
    // 非同步下載所有來源；完成後自動解析
    void fetchAll();
    // 解析本機檔案
    bool load(QString *err = nullptr);

    QStringList mcVersions(Loader loader, bool snapshots) const;
    QList<LoaderVersion> loaderVersions(Loader loader, const QString &mc) const;

signals:
    void progress(int done, int total, const QString &name);
    void finished(bool ok, const QString &err);

private:
    struct Source {
        QString url;
        QString file;
        bool required;
    };
    static QList<Source> sources();
    void fetchNext(int index, QStringList failures);

    void parseMojang(const QByteArray &d);
    void parseMeta(const QByteArray &game, const QByteArray &loader, QMap<QString, bool> *games, QList<LoaderVersion> *loaders);
    void parseForge(const QByteArray &xml, const QByteArray &promos);
    void parseNeoForge(const QByteArray &xml);
    void parseOptiFine(const QByteArray &html);
    static QStringList xmlVersions(const QByteArray &xml);
    static QByteArray readLocal(const QString &file);

    QList<VanillaVersion> m_vanilla;
    QSet<QString> m_vanillaIds;
    QMap<QString, bool> m_fabricGames, m_quiltGames; // mc → stable
    QList<LoaderVersion> m_fabricLoaders, m_quiltLoaders;
    QMap<QString, QList<LoaderVersion>> m_forge, m_neo;
    QList<LoaderVersion> m_optifine;
};

} // namespace MI
