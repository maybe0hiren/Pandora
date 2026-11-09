#pragma once
#include "core/types.h"
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

namespace pandora {
class PeerNode final : public QObject {
    Q_OBJECT
public:
    explicit PeerNode(QObject* parent = nullptr);
    bool start(quint16 tcpPort = 43170, quint16 discoveryPort = 43171);
    void stop();
    quint16 port() const;
    void announce(const QJsonObject& payload);
    void broadcastEvent(const PandoraEvent& event);
    void sendTo(const QString& address, quint16 port, const QJsonObject& message);
signals:
    void messageReceived(const QJsonObject& message, const QString& peerAddress);
    void peerDiscovered(const QString& address, quint16 port, const QString& pandoraId);
    void statusChanged(const QString& status);
private slots:
    void acceptConnections();
    void readSocket();
    void readDiscovery();
private:
    void writeJson(QTcpSocket* socket, const QJsonObject& object);
    QTcpServer server_;
    QUdpSocket discovery_;
    QHash<QTcpSocket*, QByteArray> buffers_;
    quint16 discoveryPort_{43171};
};
}
