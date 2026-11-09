#include "core/types.h"

namespace pandora {

QJsonObject DiscoveredSession::toJson() const {
    return {
        {"pandoraId", pandoraId},
        {"name", name},
        {"hostNodeId", hostNodeId},
        {"hostName", hostName},
        {"address", address},
        {"port", static_cast<int>(port)},
        {"nodeCount", nodeCount},
        {"lastSeen", lastSeen.toUTC().toString(Qt::ISODateWithMs)}
    };
}

DiscoveredSession DiscoveredSession::fromJson(const QJsonObject& j) {
    DiscoveredSession d;
    d.pandoraId = j.value("pandoraId").toString();
    d.name = j.value("name").toString();
    d.hostNodeId = j.value("hostNodeId").toString();
    d.hostName = j.value("hostName").toString();
    d.address = j.value("address").toString();
    d.port = static_cast<quint16>(j.value("port").toInt(43170));
    d.nodeCount = j.value("nodeCount").toInt(1);
    d.lastSeen = QDateTime::fromString(j.value("lastSeen").toString(), Qt::ISODateWithMs);
    if (!d.lastSeen.isValid()) d.lastSeen = QDateTime::currentDateTimeUtc();
    return d;
}

QJsonObject NodeInfo::toJson() const {
    return {
        {"id", id.toString(QUuid::WithoutBraces)},
        {"name", name},
        {"address", address},
        {"port", static_cast<int>(port)},
        {"connected", connected},
        {"managedBytes", QString::number(managedBytes)},
        {"quotaBytes", QString::number(quotaBytes)},
        {"availableDiskBytes", QString::number(availableDiskBytes)},
        {"joinedAt", joinedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"lastSeenUtc", lastSeenUtc.toUTC().toString(Qt::ISODateWithMs)},
        {"electionTerm", QString::number(electionTerm)}
    };
}

NodeInfo NodeInfo::fromJson(const QJsonObject& j) {
    NodeInfo n;
    n.id = QUuid(j.value("id").toString());
    n.name = j.value("name").toString();
    n.address = j.value("address").toString();
    n.port = static_cast<quint16>(j.value("port").toInt());
    n.connected = j.value("connected").toBool();
    if (j.value("managedBytes").isString())
        n.managedBytes = j.value("managedBytes").toString().toLongLong();
    else
        n.managedBytes = static_cast<qint64>(j.value("managedBytes").toDouble());

    if (j.value("quotaBytes").isString())
        n.quotaBytes = j.value("quotaBytes").toString().toLongLong();
    else if (j.contains("quotaBytes"))
        n.quotaBytes = static_cast<qint64>(j.value("quotaBytes").toDouble());

    if (j.value("availableDiskBytes").isString())
        n.availableDiskBytes = j.value("availableDiskBytes").toString().toLongLong();
    else
        n.availableDiskBytes = static_cast<qint64>(j.value("availableDiskBytes").toDouble());

    n.joinedAt = QDateTime::fromString(j.value("joinedAt").toString(), Qt::ISODateWithMs);
    if (!n.joinedAt.isValid()) n.joinedAt = QDateTime::currentDateTimeUtc();
    n.lastSeenUtc = QDateTime::fromString(j.value("lastSeenUtc").toString(), Qt::ISODateWithMs);
    if (!n.lastSeenUtc.isValid()) n.lastSeenUtc = QDateTime::currentDateTimeUtc();
    n.electionTerm = j.value("electionTerm").toString("1").toULongLong();
    if (n.electionTerm == 0) n.electionTerm = 1;
    return n;
}

QJsonObject FileInfo::toJson() const {
    QJsonArray r;
    for (const auto& item : replicas) r.append(item);
    QJsonObject vv;
    for (auto it = versionVector.begin(); it != versionVector.end(); ++it) {
        vv.insert(it.key(), QString::number(it.value()));
    }
    return {
        {"id", id.toString(QUuid::WithoutBraces)},
        {"name", name},
        {"folderGroup", folderGroup},
        {"size", QString::number(size)},
        {"sha256", sha256},
        {"version", QString::number(version)},
        {"modifiedBy", modifiedBy.toString(QUuid::WithoutBraces)},
        {"modifiedAt", modifiedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"replicas", r},
        {"tombstone", tombstone},
        {"versionVector", vv}
    };
}

FileInfo FileInfo::fromJson(const QJsonObject& j) {
    FileInfo f;
    f.id = QUuid(j.value("id").toString());
    f.name = j.value("name").toString();
    f.folderGroup = j.value("folderGroup").toString();
    if (j.value("size").isString())
        f.size = j.value("size").toString().toLongLong();
    else
        f.size = static_cast<qint64>(j.value("size").toDouble());

    f.sha256 = j.value("sha256").toString();
    if (j.value("version").isString())
        f.version = j.value("version").toString().toULongLong();
    else
        f.version = static_cast<quint64>(j.value("version").toDouble(1));
    if (f.version == 0) f.version = 1;

    f.modifiedBy = QUuid(j.value("modifiedBy").toString());
    f.modifiedAt = QDateTime::fromString(j.value("modifiedAt").toString(), Qt::ISODateWithMs);
    for (const auto& v : j.value("replicas").toArray()) {
        const QString rep = v.toString();
        if (!rep.isEmpty() && !f.replicas.contains(rep)) f.replicas.append(rep);
    }
    f.tombstone = j.value("tombstone").toBool();
    const QJsonObject vv = j.value("versionVector").toObject();
    for (auto it = vv.begin(); it != vv.end(); ++it) {
        f.versionVector.insert(it.key(), it.value().toString("1").toULongLong());
    }
    return f;
}

QJsonObject PandoraSession::toJson() const {
    return {
        {"id", id.toString(QUuid::WithoutBraces)},
        {"name", name},
        {"guardianNodeId", guardianNodeId},
        {"salt", QString::fromLatin1(salt.toHex())},
        {"keyVerifier", QString::fromLatin1(keyVerifier.toHex())},
        {"replicationFactor", replicationFactor},
        {"term", QString::number(term)},
        {"active", active},
        {"shuttingDown", shuttingDown},
        {"shutdownDeadlineUtc", shutdownDeadlineUtc.isValid() ? shutdownDeadlineUtc.toUTC().toString(Qt::ISODateWithMs) : QString()}
    };
}

PandoraSession PandoraSession::fromJson(const QJsonObject& j) {
    PandoraSession s;
    s.id = QUuid(j.value("id").toString());
    s.name = j.value("name").toString();
    s.guardianNodeId = j.value("guardianNodeId").toString();
    s.salt = QByteArray::fromHex(j.value("salt").toString().toLatin1());
    s.keyVerifier = QByteArray::fromHex(j.value("keyVerifier").toString().toLatin1());
    s.replicationFactor = qMax(1, j.value("replicationFactor").toInt(2));
    s.term = j.value("term").toString("1").toULongLong();
    if (s.term == 0) s.term = 1;
    s.active = j.value("active").toBool();
    s.shuttingDown = j.value("shuttingDown").toBool();
    const QString deadline = j.value("shutdownDeadlineUtc").toString();
    if (!deadline.isEmpty()) {
        s.shutdownDeadlineUtc = QDateTime::fromString(deadline, Qt::ISODateWithMs);
    }
    return s;
}

QString PandoraEvent::typeName() const {
    switch (type) {
    case EventType::SessionCreated: return "SessionCreated";
    case EventType::NodeJoined: return "NodeJoined";
    case EventType::NodeLeft: return "NodeLeft";
    case EventType::NodeUpdated: return "NodeUpdated";
    case EventType::GuardianChanged: return "GuardianChanged";
    case EventType::FileAdded: return "FileAdded";
    case EventType::FileUpdated: return "FileUpdated";
    case EventType::FileDeleted: return "FileDeleted";
    case EventType::ReplicaChanged: return "ReplicaChanged";
    case EventType::ShutdownWarning: return "ShutdownWarning";
    case EventType::ShutdownCancelled: return "ShutdownCancelled";
    case EventType::SessionDeactivated: return "SessionDeactivated";
    case EventType::MetadataSnapshot: return "MetadataSnapshot";
    }
    return "MetadataSnapshot";
}

EventType PandoraEvent::typeFromName(const QString& s) {
    if (s == "SessionCreated") return EventType::SessionCreated;
    if (s == "NodeJoined") return EventType::NodeJoined;
    if (s == "NodeLeft") return EventType::NodeLeft;
    if (s == "NodeUpdated") return EventType::NodeUpdated;
    if (s == "GuardianChanged") return EventType::GuardianChanged;
    if (s == "FileAdded") return EventType::FileAdded;
    if (s == "FileUpdated") return EventType::FileUpdated;
    if (s == "FileDeleted") return EventType::FileDeleted;
    if (s == "ReplicaChanged") return EventType::ReplicaChanged;
    if (s == "ShutdownWarning") return EventType::ShutdownWarning;
    if (s == "ShutdownCancelled") return EventType::ShutdownCancelled;
    if (s == "SessionDeactivated") return EventType::SessionDeactivated;
    return EventType::MetadataSnapshot;
}

QJsonObject PandoraEvent::toJson() const {
    return {
        {"eventId", eventId.toString(QUuid::WithoutBraces)},
        {"type", typeName()},
        {"originNodeId", originNodeId},
        {"sequence", QString::number(sequence)},
        {"term", QString::number(term)},
        {"timestamp", timestamp.toUTC().toString(Qt::ISODateWithMs)},
        {"payload", payload}
    };
}

PandoraEvent PandoraEvent::fromJson(const QJsonObject& j) {
    PandoraEvent e;
    e.eventId = QUuid(j.value("eventId").toString());
    e.type = typeFromName(j.value("type").toString());
    e.originNodeId = j.value("originNodeId").toString();
    e.sequence = j.value("sequence").toString("0").toULongLong();
    e.term = j.value("term").toString("1").toULongLong();
    e.timestamp = QDateTime::fromString(j.value("timestamp").toString(), Qt::ISODateWithMs);
    e.payload = j.value("payload").toObject();
    return e;
}

} // namespace pandora
