#pragma once

#include "core/types.h"
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

namespace pandora {

class PeerNode final : public QObject {
    Q_OBJECT
public:
    static constexpr qsizetype kMaxFrameBytes = 8 * 1024 * 1024; // 8 MiB max frame

    explicit PeerNode(QObject* parent = nullptr);
    ~PeerNode() override;

    void setLocalNodeId(const QString& nodeId) { localNodeId_ = nodeId; }
    QString localNodeId() const { return localNodeId_; }

    void setTransportKey(const QByteArray& key);
    void clearTransportKey();
    bool hasTransportKey() const { return !transportKey_.isEmpty(); }

    bool start(quint16 tcpPort = 43170, quint16 discoveryPort = 43171);
    bool startDiscoveryOnly(quint16 discoveryPort = 43171);
    void stop();
    bool isRunning() const { return server_.isListening(); }
    quint16 port() const;

    void announce(const QJsonObject& payload);
    void broadcastEvent(const PandoraEvent& event);
    void broadcastJson(const QJsonObject& message, bool authenticatedOnly = true);

    bool sendToNode(const QString& nodeId, const QJsonObject& message);
    void sendToAddress(const QString& address, quint16 port, const QJsonObject& message, const QString& expectedNodeId = {});
    QTcpSocket* connectToPeer(const QString& address, quint16 port, const QString& peerNodeId = {});

    void markSocketAuthenticated(QTcpSocket* socket, const QString& peerNodeId, const QString& peerAddress, quint16 peerListenPort);
    void disconnectSocket(QTcpSocket* socket);
    bool isNodeConnected(const QString& nodeId) const;
    QStringList connectedNodeIds() const;
    QString peerAddressForNode(const QString& nodeId) const;

signals:
    void messageReceived(const QJsonObject& message, const QString& peerNodeId, QTcpSocket* socket, const QString& peerAddress);
    void sessionDiscovered(const pandora::DiscoveredSession& session);
    void peerConnected(const QString& nodeId, const QString& address, quint16 port);
    void peerDisconnected(const QString& nodeId);
    void statusChanged(const QString& status);

private slots:
    void acceptConnections();
    void readSocket();
    void onSocketDisconnected();
    void readDiscovery();
    void onHeartbeatTick();

private:
    struct PeerConnection {
        QTcpSocket* socket{nullptr};
        QByteArray buffer;
        QString nodeId;
        QString address;
        quint16 listenPort{0};
        bool authenticated{false};
        QDateTime lastActivityUtc;
        quint64 sendSeq{0};
        quint64 lastRecvSeq{0};
        QList<QJsonObject> pendingOutbox;
    };

    void attachSocket(QTcpSocket* socket, const QString& expectedNodeId = {}, quint16 listenPort = 0);
    void writeFrame(QTcpSocket* socket, const QJsonObject& object, bool sealIfPossible = true);

    QString localNodeId_;
    QByteArray transportKey_;
    QTcpServer server_;
    QUdpSocket discovery_;
    QHash<QTcpSocket*, PeerConnection> connections_;
    QHash<QString, QTcpSocket*> nodeSockets_;
    QTimer heartbeatTimer_;
    quint16 discoveryPort_{43171};
};

} // namespace pandora
