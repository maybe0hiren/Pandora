#include "core/types.h"
#include <QJsonArray>

namespace pandora {
QJsonObject NodeInfo::toJson() const {
    return {{ "id", id.toString(QUuid::WithoutBraces) }, {"name", name},
             {"address", address}, {"port", static_cast<int>(port)},
             {"connected", connected}, {"managedBytes", static_cast<double>(managedBytes)}};
}
NodeInfo NodeInfo::fromJson(const QJsonObject& j) {
    NodeInfo n;
    n.id = QUuid(j.value("id").toString());
    n.name = j.value("name").toString();
    n.address = j.value("address").toString();
    n.port = static_cast<quint16>(j.value("port").toInt());
    n.connected = j.value("connected").toBool();
    n.managedBytes = static_cast<qint64>(j.value("managedBytes").toDouble());
    return n;
}
QJsonObject FileInfo::toJson() const {
    QJsonArray r;
    for (const auto& item : replicas) r.append(item);
    return {{"id", id.toString(QUuid::WithoutBraces)}, {"name", name},
            {"size", static_cast<double>(size)}, {"sha256", sha256},
            {"version", static_cast<double>(version)},
            {"modifiedBy", modifiedBy.toString(QUuid::WithoutBraces)},
            {"modifiedAt", modifiedAt.toUTC().toString(Qt::ISODateWithMs)},
            {"replicas", r}, {"tombstone", tombstone}};
}
FileInfo FileInfo::fromJson(const QJsonObject& j) {
    FileInfo f;
    f.id = QUuid(j.value("id").toString());
    f.name = j.value("name").toString();
    f.size = static_cast<qint64>(j.value("size").toDouble());
    f.sha256 = j.value("sha256").toString();
    f.version = static_cast<quint64>(j.value("version").toDouble(1));
    f.modifiedBy = QUuid(j.value("modifiedBy").toString());
    f.modifiedAt = QDateTime::fromString(j.value("modifiedAt").toString(), Qt::ISODateWithMs);
    for (const auto& v : j.value("replicas").toArray()) f.replicas.append(v.toString());
    f.tombstone = j.value("tombstone").toBool();
    return f;
}
QString PandoraEvent::typeName() const {
    switch (type) {
    case EventType::SessionCreated: return "SessionCreated";
    case EventType::NodeJoined: return "NodeJoined";
    case EventType::NodeLeft: return "NodeLeft";
    case EventType::GuardianChanged: return "GuardianChanged";
    case EventType::FileAdded: return "FileAdded";
    case EventType::FileUpdated: return "FileUpdated";
    case EventType::FileDeleted: return "FileDeleted";
    case EventType::ReplicaChanged: return "ReplicaChanged";
    case EventType::ShutdownWarning: return "ShutdownWarning";
    case EventType::SessionDeactivated: return "SessionDeactivated";
    case EventType::MetadataSnapshot: return "MetadataSnapshot";
    }
    return "MetadataSnapshot";
}
EventType PandoraEvent::typeFromName(const QString& s) {
    if (s == "SessionCreated") return EventType::SessionCreated;
    if (s == "NodeJoined") return EventType::NodeJoined;
    if (s == "NodeLeft") return EventType::NodeLeft;
    if (s == "GuardianChanged") return EventType::GuardianChanged;
    if (s == "FileAdded") return EventType::FileAdded;
    if (s == "FileUpdated") return EventType::FileUpdated;
    if (s == "FileDeleted") return EventType::FileDeleted;
    if (s == "ReplicaChanged") return EventType::ReplicaChanged;
    if (s == "ShutdownWarning") return EventType::ShutdownWarning;
    if (s == "SessionDeactivated") return EventType::SessionDeactivated;
    return EventType::MetadataSnapshot;
}
QJsonObject PandoraEvent::toJson() const {
    return {{"eventId", eventId.toString(QUuid::WithoutBraces)}, {"type", typeName()},
            {"originNodeId", originNodeId},
            {"timestamp", timestamp.toUTC().toString(Qt::ISODateWithMs)}, {"payload", payload}};
}
PandoraEvent PandoraEvent::fromJson(const QJsonObject& j) {
    PandoraEvent e;
    e.eventId = QUuid(j.value("eventId").toString());
    e.type = typeFromName(j.value("type").toString());
    e.originNodeId = j.value("originNodeId").toString();
    e.timestamp = QDateTime::fromString(j.value("timestamp").toString(), Qt::ISODateWithMs);
    e.payload = j.value("payload").toObject();
    return e;
}
} // namespace pandora
