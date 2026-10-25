#pragma once
/**
 * @file FileMetadata.h
 * @brief Metadata of one file stored in Pandora (never the content).
 *
 * SHARED CONTRACT - identical in all four modules.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace pandora::shared {

struct FileMetadata {
    QString fileId;                ///< Immutable unique id of the file (UUID). A re-upload gets a new id.
    QString name;                  ///< Unique file name inside Pandora (may contain a relative folder path).
    qint64 sizeBytes = 0;
    qint64 version = 0;            ///< 1 for the first upload, +1 for every edit.
    qint64 modifiedAtMs = 0;       ///< Time of the last write, ms since epoch.
    QString modifiedBy;            ///< Node UUID of the last writer (deterministic tie breaker).
    QString checksum;              ///< Content checksum (hex), computed by Storage.
    QStringList replicaNodeIds;    ///< Na'vi that hold a replica.

    bool isValid() const { return !fileId.isEmpty() && !name.isEmpty(); }

    /**
     * Last-Write-Wins ordering. Compares (version, modifiedAtMs, modifiedBy).
     * The node UUID is only a deterministic tie breaker.
     * @return <0 if this is older than @p o, 0 if the same write, >0 if newer.
     */
    int compareWrite(const FileMetadata& o) const {
        if (version != o.version) return version < o.version ? -1 : 1;
        if (modifiedAtMs != o.modifiedAtMs) return modifiedAtMs < o.modifiedAtMs ? -1 : 1;
        const int c = QString::compare(modifiedBy, o.modifiedBy);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }

    /// True if this write wins over @p o under Last-Write-Wins.
    bool supersedes(const FileMetadata& o) const { return compareWrite(o) > 0; }

    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("fileId"), fileId);
        o.insert(QStringLiteral("name"), name);
        o.insert(QStringLiteral("sizeBytes"), static_cast<double>(sizeBytes));
        o.insert(QStringLiteral("version"), static_cast<double>(version));
        o.insert(QStringLiteral("modifiedAtMs"), static_cast<double>(modifiedAtMs));
        o.insert(QStringLiteral("modifiedBy"), modifiedBy);
        o.insert(QStringLiteral("checksum"), checksum);
        o.insert(QStringLiteral("replicaNodeIds"), QJsonArray::fromStringList(replicaNodeIds));
        return o;
    }

    static FileMetadata fromJson(const QJsonObject& o) {
        FileMetadata f;
        f.fileId = o.value(QStringLiteral("fileId")).toString();
        f.name = o.value(QStringLiteral("name")).toString();
        f.sizeBytes = static_cast<qint64>(o.value(QStringLiteral("sizeBytes")).toDouble());
        f.version = static_cast<qint64>(o.value(QStringLiteral("version")).toDouble());
        f.modifiedAtMs = static_cast<qint64>(o.value(QStringLiteral("modifiedAtMs")).toDouble());
        f.modifiedBy = o.value(QStringLiteral("modifiedBy")).toString();
        f.checksum = o.value(QStringLiteral("checksum")).toString();
        const QJsonArray a = o.value(QStringLiteral("replicaNodeIds")).toArray();
        for (const QJsonValue& v : a) f.replicaNodeIds << v.toString();
        return f;
    }
};

}  // namespace pandora::shared
