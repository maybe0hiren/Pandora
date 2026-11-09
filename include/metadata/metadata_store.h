#pragma once

#include "core/types.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>

namespace pandora {

class MetadataStore final : public QObject {
    Q_OBJECT
public:
    explicit MetadataStore(QObject* parent = nullptr);
    ~MetadataStore() override;

    bool initializePersistence(const QString& sqliteFilePath);
    void reset();

    bool addNode(const NodeInfo& node);
    bool updateNodeStatus(const QString& nodeId, bool connected, qint64 managedBytes = -1, qint64 availableDiskBytes = -1);
    void removeNode(const QString& nodeId);
    NodeInfo node(const QString& nodeId) const;
    QList<NodeInfo> nodes(bool connectedOnly = false) const;

    bool addFile(const FileInfo& file, QString* error = nullptr);
    bool updateFile(const FileInfo& file, QString* error = nullptr);
    bool deleteFile(const QString& fileId, const QUuid& actor, QString* error = nullptr);
    bool addReplicaToFile(const QString& fileId, const QString& nodeId);
    bool removeReplicaFromFile(const QString& fileId, const QString& nodeId);
    FileInfo file(const QString& fileId, bool* found = nullptr) const;
    QList<FileInfo> files(bool includeDeleted = false) const;
    bool applyRemoteFile(const FileInfo& file);

    void setGuardian(const QString& nodeId, quint64 term = 0);
    QString guardian() const;
    quint64 term() const;

    // Deterministic Guardian Election (highest connected node with lowest joinedAt / lexicographical ID)
    QString electDeterministicGuardian() const;

    void setSession(const PandoraSession& session);
    PandoraSession session() const;

    // Event log & deduplication
    bool hasSeenEvent(const QUuid& eventId) const;
    bool recordEvent(const PandoraEvent& event);
    QList<PandoraEvent> recentEvents(int limit = 100) const;

    QJsonObject snapshot() const;
    void applySnapshot(const QJsonObject& snapshot);

signals:
    void changed();

private:
    static bool isFileNewer(const FileInfo& incoming, const FileInfo& existing);
    void persistFileUnlocked(const FileInfo& f);
    void persistNodeUnlocked(const NodeInfo& n);
    void persistEventUnlocked(const PandoraEvent& e);

    mutable QMutex mutex_;
    QHash<QString, NodeInfo> nodes_;
    QHash<QString, FileInfo> files_;
    QSet<QString> seenEvents_;
    QList<PandoraEvent> eventHistory_;
    QString guardian_;
    PandoraSession session_;
    QString dbConnectionName_;
    bool dbOpen_{false};
};

} // namespace pandora
