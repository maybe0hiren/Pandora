#pragma once
/**
 * @file Protocol.h
 * @brief Message types and payload field names of the Pandora wire protocol.
 *
 * SHARED CONTRACT - identical in all four modules.
 *
 * Payload layout per message type (all payloads are JSON objects):
 *
 * | Type               | Payload                                                          |
 * |--------------------|------------------------------------------------------------------|
 * | CREATE_PANDORA     | pandora:{Pandora}, node:{Node}                                   |
 * | JOIN_REQUEST       | node:{Node}  (key proof is added by the Security module)         |
 * | JOIN_ACCEPTED      | snapshot:{full metadata snapshot}   (addressed to the joiner)    |
 * | NAVI_JOINED        | node:{Node}                                                      |
 * | NAVI_LEFT          | naviId                                                           |
 * | FILE_UPLOAD        | file:{FileMetadata}  (includes chosen replicaNodeIds)            |
 * | FILE_UPDATE        | file:{FileMetadata}  (new version)                               |
 * | FILE_DELETE        | fileId                                                           |
 * | FILE_REQUEST       | fileId, ...   (storage <-> storage, ignored by metadata)         |
 * | FILE_RESPONSE      | fileId, ...   (storage <-> storage, ignored by metadata)         |
 * | METADATA_SYNC      | kind = "snapshot_request" | "snapshot" | "replica_update"        |
 * |                    |   snapshot: snapshot:{...}                                       |
 * |                    |   replica_update: fileId, addReplicas[], removeReplicas[]        |
 * |                    |   logs_request: (empty, Guardian -> all)                         |
 * |                    |   logs: logs:[{seq,timestampMs,level,category,message}] (-> Guardian) |
 * | GUARDIAN_CHANGED   | oldGuardianId, newGuardianId, epoch                              |
 * | SHUTDOWN_WARNING   | secondsRemaining                                                 |
 * | PANDORA_DEACTIVATE | (empty)                                                          |
 */

#include <QLatin1String>
#include <QString>

namespace pandora::shared {

/// All message types understood by Pandora. Networking transports them, other modules interpret them.
enum class MessageType {
    Unknown = 0,
    CreatePandora,
    JoinRequest,
    JoinAccepted,
    NaviJoined,
    NaviLeft,
    FileUpload,
    FileUpdate,
    FileDelete,
    FileRequest,
    FileResponse,
    MetadataSync,
    GuardianChanged,
    ShutdownWarning,
    PandoraDeactivate
};

/// Wire name of a message type, e.g. MessageType::NaviJoined -> "NAVI_JOINED".
inline QString messageTypeToString(MessageType t) {
    switch (t) {
    case MessageType::CreatePandora:     return QStringLiteral("CREATE_PANDORA");
    case MessageType::JoinRequest:       return QStringLiteral("JOIN_REQUEST");
    case MessageType::JoinAccepted:      return QStringLiteral("JOIN_ACCEPTED");
    case MessageType::NaviJoined:        return QStringLiteral("NAVI_JOINED");
    case MessageType::NaviLeft:          return QStringLiteral("NAVI_LEFT");
    case MessageType::FileUpload:        return QStringLiteral("FILE_UPLOAD");
    case MessageType::FileUpdate:        return QStringLiteral("FILE_UPDATE");
    case MessageType::FileDelete:        return QStringLiteral("FILE_DELETE");
    case MessageType::FileRequest:       return QStringLiteral("FILE_REQUEST");
    case MessageType::FileResponse:      return QStringLiteral("FILE_RESPONSE");
    case MessageType::MetadataSync:      return QStringLiteral("METADATA_SYNC");
    case MessageType::GuardianChanged:   return QStringLiteral("GUARDIAN_CHANGED");
    case MessageType::ShutdownWarning:   return QStringLiteral("SHUTDOWN_WARNING");
    case MessageType::PandoraDeactivate: return QStringLiteral("PANDORA_DEACTIVATE");
    case MessageType::Unknown:           break;
    }
    return QStringLiteral("UNKNOWN");
}

/// Inverse of messageTypeToString(); returns MessageType::Unknown for unrecognised names.
inline MessageType messageTypeFromString(const QString& s) {
    static const MessageType all[] = {
        MessageType::CreatePandora, MessageType::JoinRequest,     MessageType::JoinAccepted,
        MessageType::NaviJoined,    MessageType::NaviLeft,        MessageType::FileUpload,
        MessageType::FileUpdate,    MessageType::FileDelete,      MessageType::FileRequest,
        MessageType::FileResponse,  MessageType::MetadataSync,    MessageType::GuardianChanged,
        MessageType::ShutdownWarning, MessageType::PandoraDeactivate};
    for (MessageType t : all)
        if (messageTypeToString(t) == s) return t;
    return MessageType::Unknown;
}

/// JSON keys used in payloads.
namespace key {
inline constexpr QLatin1String kPandora{"pandora"};
inline constexpr QLatin1String kNode{"node"};
inline constexpr QLatin1String kNaviId{"naviId"};
inline constexpr QLatin1String kFile{"file"};
inline constexpr QLatin1String kFileId{"fileId"};
inline constexpr QLatin1String kSnapshot{"snapshot"};
inline constexpr QLatin1String kKind{"kind"};
inline constexpr QLatin1String kAddReplicas{"addReplicas"};
inline constexpr QLatin1String kRemoveReplicas{"removeReplicas"};
inline constexpr QLatin1String kOldGuardianId{"oldGuardianId"};
inline constexpr QLatin1String kNewGuardianId{"newGuardianId"};
inline constexpr QLatin1String kEpoch{"epoch"};
inline constexpr QLatin1String kSecondsRemaining{"secondsRemaining"};
inline constexpr QLatin1String kLogs{"logs"};
}  // namespace key

/// Values of the "kind" field of METADATA_SYNC.
namespace sync {
inline constexpr QLatin1String kSnapshotRequest{"snapshot_request"};
inline constexpr QLatin1String kSnapshot{"snapshot"};
inline constexpr QLatin1String kReplicaUpdate{"replica_update"};
inline constexpr QLatin1String kLogsRequest{"logs_request"};
inline constexpr QLatin1String kLogs{"logs"};
}  // namespace sync

}  // namespace pandora::shared
