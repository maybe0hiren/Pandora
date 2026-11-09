#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace pandora {

struct DiscoveredSession {
    QString pandoraId;
    QString name;
    QString hostNodeId;
    QString hostName;
    QString address;
    quint16 port{43170};
    int nodeCount{1};
    QDateTime lastSeen;

    QJsonObject toJson() const;
    static DiscoveredSession fromJson(const QJsonObject& json);
};

struct NodeInfo {
    QUuid id;
    QString name;
    QString address;
    quint16 port{0};
    bool connected{false};
    qint64 managedBytes{0};
    qint64 quotaBytes{5LL * 1024 * 1024 * 1024}; // Default 5 GiB allocation
    qint64 availableDiskBytes{0};
    QDateTime joinedAt;
    QDateTime lastSeenUtc;
    quint64 electionTerm{1};

    QJsonObject toJson() const;
    static NodeInfo fromJson(const QJsonObject& json);
};

struct FileInfo {
    QUuid id;
    QString name;         // Relative path inside Pandora (e.g. "report.pdf" or "docs/spec.md")
    QString folderGroup;  // Empty for single file, top-level folder name for folder uploads
    qint64 size{0};
    QString sha256;
    quint64 version{1};
    QUuid modifiedBy;
    QDateTime modifiedAt;
    QStringList replicas; // Node IDs holding verified physical replicas
    bool tombstone{false};
    QMap<QString, quint64> versionVector;

    QJsonObject toJson() const;
    static FileInfo fromJson(const QJsonObject& json);
};

struct PandoraSession {
    QUuid id;
    QString name;
    QString guardianNodeId;
    QByteArray salt;
    QByteArray keyVerifier;
    int replicationFactor{2};
    quint64 term{1};
    bool active{false};
    bool shuttingDown{false};
    QDateTime shutdownDeadlineUtc;

    QJsonObject toJson() const;
    static PandoraSession fromJson(const QJsonObject& json);
};

enum class EventType {
    SessionCreated,
    NodeJoined,
    NodeLeft,
    NodeUpdated,
    GuardianChanged,
    FileAdded,
    FileUpdated,
    FileDeleted,
    ReplicaChanged,
    ShutdownWarning,
    ShutdownCancelled,
    SessionDeactivated,
    MetadataSnapshot
};

struct PandoraEvent {
    QUuid eventId{QUuid::createUuid()};
    EventType type{EventType::MetadataSnapshot};
    QString originNodeId;
    quint64 sequence{0};
    quint64 term{1};
    QDateTime timestamp{QDateTime::currentDateTimeUtc()};
    QJsonObject payload;

    QJsonObject toJson() const;
    static PandoraEvent fromJson(const QJsonObject& json);
    QString typeName() const;
    static EventType typeFromName(const QString& name);
};

struct TransferProgress {
    QString transferId;
    QString fileId;
    QString fileName;
    QString peerNodeId;
    QString direction; // "Upload" or "Download" or "Repair"
    qint64 bytesTransferred{0};
    qint64 totalBytes{0};
    bool completed{false};
    bool failed{false};
    QString error;
};

} // namespace pandora
