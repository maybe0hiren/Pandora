#pragma once
/**
 * @file MetadataStore.h
 * @brief PRIVATE. Thin SQLite data-access layer used only by MetadataService.
 *
 * Not installed, not part of the public API. Contains no protocol or lifecycle logic: just SQL.
 */

#include <QList>
#include <QPair>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <pandora/metadata/MetadataService.h>
#include <pandora/shared/FileMetadata.h>
#include <pandora/shared/Node.h>
#include <pandora/shared/Pandora.h>

namespace pandora::metadata {

class MetadataStore {
public:
    MetadataStore();
    ~MetadataStore();
    MetadataStore(const MetadataStore&) = delete;
    MetadataStore& operator=(const MetadataStore&) = delete;

    bool open(const QString& path, QString* error);
    bool isOpen() const;
    /// Closes the connection and deletes the database file (and -wal/-shm/-journal) if it was file based.
    void closeAndErase();

    // pandora (single row)
    bool savePandora(const pandora::shared::Pandora& p);
    bool loadPandora(pandora::shared::Pandora* out) const;

    // Na'vi
    bool upsertNavi(const pandora::shared::Node& n);
    bool setNaviConnected(const QString& id, bool connected);
    bool getNavi(const QString& id, pandora::shared::Node* out) const;
    QList<pandora::shared::Node> navis(bool onlyConnected) const;  ///< includes derived storageUsedBytes
    int connectedCount() const;

    // files & replicas
    /// Inserts or replaces the file row and replaces its replica set (unknown Na'vi ids are skipped).
    bool upsertFile(const pandora::shared::FileMetadata& f);
    bool deleteFile(const QString& fileId);
    bool getFile(const QString& fileId, pandora::shared::FileMetadata* out) const;
    bool getFileByName(const QString& name, pandora::shared::FileMetadata* out) const;
    QList<pandora::shared::FileMetadata> files() const;
    bool addReplica(const QString& fileId, const QString& naviId);
    bool removeReplica(const QString& fileId, const QString& naviId);
    /// Deletes every replica row of a node, returns the affected file ids.
    QStringList removeReplicasOfNavi(const QString& naviId);
    QStringList availableReplicas(const QString& fileId) const;  ///< connected holders only
    qint64 usage(const QString& naviId) const;

    // tombstones
    bool addTombstone(const QString& fileId, const QString& name, qint64 deletedAtMs);
    bool isTombstoned(const QString& fileId) const;
    QList<QPair<QString, QString>> tombstones() const;  ///< (fileId, name)

    // idempotency
    bool wasApplied(const QString& messageId) const;
    bool markApplied(const QString& messageId);

    // logs
    void appendLog(const QString& level, const QString& category, const QString& message);
    QList<LogEntry> logs() const;

    QString lastError() const { return m_lastError; }

private:
    QSqlDatabase db() const;
    bool runScript(const QString& script);

    QString m_connection;
    QString m_path;
    mutable QString m_lastError;
};

/// Schema text generated from sql/schema.sql at build time (see CMakeLists.txt).
const char* embeddedSchemaSql();

}  // namespace pandora::metadata
