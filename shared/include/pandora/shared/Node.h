#pragma once
/**
 * @file Node.h
 * @brief A Na'vi (participating laptop) as seen in the metadata.
 *
 * SHARED CONTRACT - identical in all four modules.
 */

#include <QJsonObject>
#include <QString>

namespace pandora::shared {

struct Node {
    QString id;                    ///< Temporary node UUID (from Security::generateNodeID()).
    QString name;                  ///< Display name typed by the user.
    QString address;               ///< Last known IP address (informational, set by Networking glue).
    quint16 port = 0;              ///< Listening TCP port.
    bool connected = true;         ///< Connection status. Once false, it never becomes true again.
    qint64 joinedAtMs = 0;         ///< Join time, ms since epoch.
    qint64 storageUsedBytes = 0;   ///< Derived value: bytes of Pandora replicas on this node.

    bool isValid() const { return !id.isEmpty() && !name.isEmpty(); }

    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("id"), id);
        o.insert(QStringLiteral("name"), name);
        o.insert(QStringLiteral("address"), address);
        o.insert(QStringLiteral("port"), static_cast<int>(port));
        o.insert(QStringLiteral("connected"), connected);
        o.insert(QStringLiteral("joinedAtMs"), static_cast<double>(joinedAtMs));
        o.insert(QStringLiteral("storageUsedBytes"), static_cast<double>(storageUsedBytes));
        return o;
    }

    static Node fromJson(const QJsonObject& o) {
        Node n;
        n.id = o.value(QStringLiteral("id")).toString();
        n.name = o.value(QStringLiteral("name")).toString();
        n.address = o.value(QStringLiteral("address")).toString();
        n.port = static_cast<quint16>(o.value(QStringLiteral("port")).toInt());
        n.connected = o.value(QStringLiteral("connected")).toBool(true);
        n.joinedAtMs = static_cast<qint64>(o.value(QStringLiteral("joinedAtMs")).toDouble());
        n.storageUsedBytes = static_cast<qint64>(o.value(QStringLiteral("storageUsedBytes")).toDouble());
        return n;
    }
};

}  // namespace pandora::shared
