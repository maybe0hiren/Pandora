#pragma once
/**
 * @file Message.h
 * @brief The envelope exchanged between Na'vi. Networking transports it, other modules interpret it.
 *
 * SHARED CONTRACT - identical in all four modules.
 */

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QUuid>

#include "Constants.h"
#include "Node.h"
#include "Protocol.h"

namespace pandora::shared {

struct Message {
    MessageType type = MessageType::Unknown;
    QString messageId;       ///< Unique id; receivers use it to drop duplicates.
    QString senderId;        ///< Node UUID of the originator.
    QString recipientId;     ///< Empty = broadcast to every Na'vi, otherwise a single node.
    qint64 timestampMs = 0;
    int protocolVersion = constants::kProtocolVersion;
    QJsonObject payload;

    bool isBroadcast() const { return recipientId.isEmpty(); }
    bool isValid() const { return type != MessageType::Unknown && !messageId.isEmpty() && !senderId.isEmpty(); }

    /// Builds a message with fresh id and timestamp.
    static Message create(MessageType type, const QString& senderId, const QJsonObject& payload = {},
                          const QString& recipientId = {}) {
        Message m;
        m.type = type;
        m.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m.senderId = senderId;
        m.recipientId = recipientId;
        m.timestampMs = QDateTime::currentMSecsSinceEpoch();
        m.payload = payload;
        return m;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("type"), messageTypeToString(type));
        o.insert(QStringLiteral("messageId"), messageId);
        o.insert(QStringLiteral("senderId"), senderId);
        o.insert(QStringLiteral("recipientId"), recipientId);
        o.insert(QStringLiteral("timestampMs"), static_cast<double>(timestampMs));
        o.insert(QStringLiteral("protocolVersion"), protocolVersion);
        o.insert(QStringLiteral("payload"), payload);
        return o;
    }

    /// Parses a message; the result's isValid() is false on malformed input.
    static Message fromJson(const QJsonObject& o) {
        Message m;
        m.type = messageTypeFromString(o.value(QStringLiteral("type")).toString());
        m.messageId = o.value(QStringLiteral("messageId")).toString();
        m.senderId = o.value(QStringLiteral("senderId")).toString();
        m.recipientId = o.value(QStringLiteral("recipientId")).toString();
        m.timestampMs = static_cast<qint64>(o.value(QStringLiteral("timestampMs")).toDouble());
        m.protocolVersion = o.value(QStringLiteral("protocolVersion")).toInt(constants::kProtocolVersion);
        m.payload = o.value(QStringLiteral("payload")).toObject();
        return m;
    }
};

}  // namespace pandora::shared

Q_DECLARE_METATYPE(pandora::shared::Message)
Q_DECLARE_METATYPE(pandora::shared::Node)
