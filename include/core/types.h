#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace pandora {

struct NodeInfo {
    QUuid id;
    QString name;
    QString address;
    quint16 port{0};
    bool connected{false};
    qint64 managedBytes{0};

    QJsonObject toJson() const;
    static NodeInfo fromJson(const QJsonObject& json);
};

struct FileInfo {
    QUuid id;
    QString name;
    qint64 size{0};
    QString sha256;
    quint64 version{1};
    QUuid modifiedBy;
    QDateTime modifiedAt;
    QStringList replicas;
    bool tombstone{false};

    QJsonObject toJson() const;
    static FileInfo fromJson(const QJsonObject& json);
};

struct PandoraSession {
    QUuid id;
    QString name;
    QString guardianNodeId;
    QByteArray keyVerifier;
    bool active{false};
};

enum class EventType {
    SessionCreated, NodeJoined, NodeLeft, GuardianChanged,
    FileAdded, FileUpdated, FileDeleted, ReplicaChanged,
    ShutdownWarning, SessionDeactivated, MetadataSnapshot
};

struct PandoraEvent {
    QUuid eventId{QUuid::createUuid()};
    EventType type{EventType::MetadataSnapshot};
    QString originNodeId;
    QDateTime timestamp{QDateTime::currentDateTimeUtc()};
    QJsonObject payload;

    QJsonObject toJson() const;
    static PandoraEvent fromJson(const QJsonObject& json);
    QString typeName() const;
    static EventType typeFromName(const QString& name);
};

} // namespace pandora
