#pragma once
#include <pandora/shared/Protocol.h>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <map>

namespace pandora {
/// maxNodes includes self and ready transport peers. Total sockets are capped at
/// twice the remote-peer limit to allow bounded simultaneous-dial handshakes.
struct NetworkConfig {
 int maxNodes = 20;
 int heartbeatMs = 2000;
 int timeoutMs = 10000;
 qint64 maxQueuedBytes = 4 * 1024 * 1024;
};
/// Event-loop confined QObject. Call every API on its owning thread.
/// Cross-thread callers must use queued invokeMethod; signals can cross threads.
/// Connections are unauthenticated transport candidates, NOT admitted Na'vi.
class NetworkManager final : public QObject {
 Q_OBJECT
public:
 explicit NetworkManager(QObject* parent = nullptr);
 ~NetworkManager() override;
 /// Starts a symmetric TCP listener. Port 0 chooses an ephemeral port.
 /// Session and node identifiers must be nonempty; no keys are accepted here.
 bool start(const Node& local, const QString& pandoraId,
            const QHostAddress& bindAddress = QHostAddress::AnyIPv4,
            quint16 port = 0, const NetworkConfig& config = {});
 /// Stops listeners and all connections; idempotent. Clears session identity.
 void stop();
 /// Asynchronous dial. True means attempt started, not handshake completed.
 bool connectToPeer(const QHostAddress& address, quint16 port);
 /// Closes this peer immediately; emits peerDisconnected exactly once.
 bool disconnectPeer(const QString& nodeId);
 /// Enqueues one bounded frame; true is NOT a delivery acknowledgement.
 /// Sender and session must match this manager. No offline queue or retry.
 bool sendMessage(const QString& nodeId, const Message&);
 /// Returns number of peers for which enqueue succeeded. Does not send to self.
 int broadcastMessage(const Message&);
 /// Snapshot of handshaken transport peers; excludes self and pending dials.
 QList<Node> getConnectedPeers() const;
 quint16 listeningPort() const;
 /// Milestone 3: enable session-scoped UDP advertisements and discovery.
 bool startDiscovery(quint16 port = DefaultDiscoveryPort, int intervalMs = 2000);
 void stopDiscovery();
signals:
 void peerConnected(const pandora::Node& peer);
 void peerDisconnected(const QString& nodeId);
 void messageReceived(const QString& nodeId, const pandora::Message& message);
 void peerDiscovered(const pandora::Node& peer);
 void networkError(const QString& description);
private:
 struct Connection {
  FrameDecoder decoder;
  Node peer;
  bool outgoing = false, ready = false;
  qint64 lastSeen = 0, openedAt = 0;
 };
 void assertThread() const;
 void attach(QTcpSocket*, bool outgoing);
 void receive(QTcpSocket*);
 void process(QTcpSocket*, const QJsonObject&);
 bool queue(QTcpSocket*, const QJsonObject&);
 // Temporary diagnostics; payloads and unknown fields are never logged.
 void diagnostic(QTcpSocket*, const QString& event, const QJsonObject& fields = {}) const;
 void drop(QTcpSocket*, const QString& reason);
 void tick();
 void advertise();
 QTcpSocket* socketFor(const QString&) const;
 QTcpServer server_;
 QUdpSocket discovery_;
 QTimer timer_, discoveryTimer_;
 QElapsedTimer clock_;
 Node local_;
 QString session_;
 NetworkConfig config_;
 quint16 discoveryPort_ = 0;
 std::map<QTcpSocket*, std::unique_ptr<Connection>> connections_;
};
}
Q_DECLARE_METATYPE(pandora::Node)
Q_DECLARE_METATYPE(pandora::Message)





