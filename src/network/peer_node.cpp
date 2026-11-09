#include "network/peer_node.h"
#include "security/session_security.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QNetworkInterface>

namespace pandora {

namespace {
QString normalizeHostAddress(const QHostAddress& addr) {
    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        bool ok = false;
        const QHostAddress ipv4(addr.toIPv4Address(&ok));
        if (ok) return ipv4.toString();
    }
    return addr.toString();
}
} // namespace

PeerNode::PeerNode(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &PeerNode::acceptConnections);
    connect(&discovery_, &QUdpSocket::readyRead, this, &PeerNode::readDiscovery);
    heartbeatTimer_.setInterval(3000);
    connect(&heartbeatTimer_, &QTimer::timeout, this, &PeerNode::onHeartbeatTick);
}

PeerNode::~PeerNode() {
    stop();
}

void PeerNode::setTransportKey(const QByteArray& key) {
    transportKey_ = key;
}

void PeerNode::clearTransportKey() {
    transportKey_.clear();
}

bool PeerNode::start(quint16 tcpPort, quint16 discoveryPort) {
    discoveryPort_ = discoveryPort;
    if (server_.isListening()) server_.close();

    // Try binding exact tcpPort first; if busy on local multi-instance testing, fall back to ephemeral port
    if (!server_.listen(QHostAddress::AnyIPv4, tcpPort)) {
        if (!server_.listen(QHostAddress::AnyIPv4, 0)) {
            emit statusChanged("TCP listen error: " + server_.errorString());
            return false;
        }
    }

    if (discovery_.state() != QAbstractSocket::BoundState) {
        if (!discovery_.bind(QHostAddress::AnyIPv4, discoveryPort_, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            emit statusChanged("UDP discovery warning: " + discovery_.errorString());
        }
    }

    heartbeatTimer_.start();
    emit statusChanged(QString("Listening on TCP %1 / UDP %2").arg(server_.serverPort()).arg(discoveryPort_));
    return true;
}

bool PeerNode::startDiscoveryOnly(quint16 discoveryPort) {
    discoveryPort_ = discoveryPort;
    if (discovery_.state() == QAbstractSocket::BoundState) return true;
    return discovery_.bind(QHostAddress::AnyIPv4, discoveryPort_, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
}

void PeerNode::stop() {
    heartbeatTimer_.stop();
    discovery_.close();
    server_.close();
    const auto sockets = connections_.keys();
    for (auto* s : sockets) {
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    connections_.clear();
    nodeSockets_.clear();
    transportKey_.clear();
    emit statusChanged("Offline");
}

quint16 PeerNode::port() const {
    return server_.serverPort();
}

void PeerNode::announce(const QJsonObject& payload) {
    if (discoveryPort_ == 0) return;
    QJsonObject j = payload;
    j["kind"] = "discovery";
    if (!localNodeId_.isEmpty()) j["hostNodeId"] = localNodeId_;
    const auto bytes = QJsonDocument(j).toJson(QJsonDocument::Compact);

    // Broadcast on 255.255.255.255, localhost, and all active IPv4 subnet broadcast addresses
    discovery_.writeDatagram(bytes, QHostAddress::Broadcast, discoveryPort_);
    discovery_.writeDatagram(bytes, QHostAddress::LocalHost, discoveryPort_);

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull()) {
                discovery_.writeDatagram(bytes, entry.broadcast(), discoveryPort_);
            }
        }
    }
}

void PeerNode::broadcastEvent(const PandoraEvent& e) {
    QJsonObject m{{"kind", "event"}, {"event", e.toJson()}};
    broadcastJson(m, true);
}

void PeerNode::broadcastJson(const QJsonObject& message, bool authenticatedOnly) {
    const auto sockets = connections_.keys();
    for (auto* socket : sockets) {
        if (!connections_.contains(socket)) continue;
        const auto& conn = connections_[socket];
        if (authenticatedOnly && !conn.authenticated) continue;
        if (socket->state() == QAbstractSocket::ConnectedState) {
            writeFrame(socket, message, conn.authenticated);
        }
    }
}

bool PeerNode::sendToNode(const QString& nodeId, const QJsonObject& message) {
    QTcpSocket* socket = nodeSockets_.value(nodeId, nullptr);
    if (!socket || !connections_.contains(socket)) return false;
    if (socket->state() == QAbstractSocket::ConnectedState) {
        writeFrame(socket, message, connections_[socket].authenticated);
        return true;
    }
    connections_[socket].pendingOutbox.append(message);
    return true;
}

void PeerNode::sendToAddress(const QString& address, quint16 port, const QJsonObject& message, const QString& expectedNodeId) {
    if (!expectedNodeId.isEmpty() && nodeSockets_.contains(expectedNodeId)) {
        QTcpSocket* existing = nodeSockets_.value(expectedNodeId);
        if (existing && connections_.contains(existing)) {
            if (existing->state() == QAbstractSocket::ConnectedState) {
                writeFrame(existing, message, connections_[existing].authenticated);
                return;
            }
            connections_[existing].pendingOutbox.append(message);
            return;
        }
    }

    QTcpSocket* socket = connectToPeer(address, port, expectedNodeId);
    if (!socket) return;
    if (socket->state() == QAbstractSocket::ConnectedState) {
        writeFrame(socket, message, connections_[socket].authenticated);
    } else {
        connections_[socket].pendingOutbox.append(message);
    }
}

QTcpSocket* PeerNode::connectToPeer(const QString& address, quint16 port, const QString& peerNodeId) {
    if (address.isEmpty() || port == 0) return nullptr;
    if (!peerNodeId.isEmpty() && peerNodeId == localNodeId_) return nullptr;

    if (!peerNodeId.isEmpty() && nodeSockets_.contains(peerNodeId)) {
        QTcpSocket* existing = nodeSockets_.value(peerNodeId);
        if (existing && (existing->state() == QAbstractSocket::ConnectedState ||
                         existing->state() == QAbstractSocket::ConnectingState)) {
            return existing;
        }
    }

    auto* socket = new QTcpSocket(this);
    attachSocket(socket, peerNodeId, port);
    connections_[socket].address = address;

    connect(socket, &QTcpSocket::connected, this, [this, socket]() {
        if (!connections_.contains(socket)) return;
        auto& conn = connections_[socket];
        conn.lastActivityUtc = QDateTime::currentDateTimeUtc();
        if (conn.address.isEmpty()) {
            conn.address = normalizeHostAddress(socket->peerAddress());
        }
        const auto pending = conn.pendingOutbox;
        conn.pendingOutbox.clear();
        for (const QJsonObject& msg : pending) {
            writeFrame(socket, msg, conn.authenticated);
        }
    });

    socket->connectToHost(address, port);
    return socket;
}

void PeerNode::markSocketAuthenticated(QTcpSocket* socket, const QString& peerNodeId, const QString& peerAddress, quint16 peerListenPort) {
    if (!socket || !connections_.contains(socket)) return;

    auto& conn = connections_[socket];
    conn.authenticated = true;
    conn.nodeId = peerNodeId;
    if (!peerAddress.isEmpty()) conn.address = peerAddress;
    if (peerListenPort > 0) conn.listenPort = peerListenPort;
    conn.lastActivityUtc = QDateTime::currentDateTimeUtc();

    if (!peerNodeId.isEmpty()) {
        QTcpSocket* prev = nodeSockets_.value(peerNodeId, nullptr);
        if (!prev || prev == socket || prev->state() != QAbstractSocket::ConnectedState || !connections_.contains(prev)) {
            nodeSockets_.insert(peerNodeId, socket);
        }
        emit peerConnected(peerNodeId, conn.address, conn.listenPort);
    }
}

void PeerNode::disconnectSocket(QTcpSocket* socket) {
    if (!socket) return;
    if (connections_.contains(socket)) {
        const QString nid = connections_[socket].nodeId;
        if (!nid.isEmpty() && nodeSockets_.value(nid) == socket) {
            nodeSockets_.remove(nid);
        }
        connections_.remove(socket);
    }
    socket->disconnect(this);
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->flush();
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            return;
        }
    }
    socket->deleteLater();
}

bool PeerNode::isNodeConnected(const QString& nodeId) const {
    QTcpSocket* s = nodeSockets_.value(nodeId, nullptr);
    return s && s->state() == QAbstractSocket::ConnectedState &&
           connections_.contains(s) && connections_[s].authenticated;
}

QStringList PeerNode::connectedNodeIds() const {
    QStringList out;
    for (auto it = nodeSockets_.begin(); it != nodeSockets_.end(); ++it) {
        if (isNodeConnected(it.key())) out.append(it.key());
    }
    return out;
}

QString PeerNode::peerAddressForNode(const QString& nodeId) const {
    QTcpSocket* s = nodeSockets_.value(nodeId, nullptr);
    if (!s || !connections_.contains(s)) return {};
    return connections_[s].address;
}

void PeerNode::acceptConnections() {
    while (server_.hasPendingConnections()) {
        auto* s = server_.nextPendingConnection();
        attachSocket(s);
    }
}

void PeerNode::attachSocket(QTcpSocket* socket, const QString& expectedNodeId, quint16 listenPort) {
    PeerConnection conn;
    conn.socket = socket;
    conn.nodeId = expectedNodeId;
    conn.address = normalizeHostAddress(socket->peerAddress());
    conn.listenPort = listenPort;
    conn.lastActivityUtc = QDateTime::currentDateTimeUtc();
    connections_.insert(socket, conn);
    if (!expectedNodeId.isEmpty() && !nodeSockets_.contains(expectedNodeId)) {
        nodeSockets_.insert(expectedNodeId, socket);
    }

    connect(socket, &QTcpSocket::readyRead, this, &PeerNode::readSocket);
    connect(socket, &QTcpSocket::disconnected, this, &PeerNode::onSocketDisconnected);
}

void PeerNode::readSocket() {
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s || !connections_.contains(s)) return;

    auto& conn = connections_[s];
    conn.lastActivityUtc = QDateTime::currentDateTimeUtc();
    conn.buffer.append(s->readAll());

    if (conn.buffer.size() > kMaxFrameBytes) {
        emit statusChanged(QString("Dropped peer connection from %1 (frame exceeded %2 bytes)")
                           .arg(conn.address).arg(kMaxFrameBytes));
        disconnectSocket(s);
        return;
    }

    while (connections_.contains(s)) {
        const auto newline = conn.buffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = conn.buffer.left(newline).trimmed();
        conn.buffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;

        QJsonParseError parseErr;
        const auto doc = QJsonDocument::fromJson(line, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        QJsonObject obj = doc.object();
        const QString kind = obj.value("kind").toString();

        // Unseal AEAD envelope if sealed
        if (kind == "sealed") {
            if (transportKey_.isEmpty()) continue;
            QJsonObject inner;
            quint64 seq = 0;
            if (!SessionSecurity::openJson(obj, transportKey_, &inner, &seq)) {
                emit statusChanged(QString("Tampered or invalid encrypted frame rejected from %1").arg(conn.address));
                continue;
            }
            if (seq > 0 && seq <= conn.lastRecvSeq) {
                // Replay protection
                continue;
            }
            if (seq > 0) conn.lastRecvSeq = seq;
            obj = inner;
        }

        const QString innerKind = obj.value("kind").toString();
        if (innerKind == "ping") {
            writeFrame(s, {{"kind", "pong"}, {"nodeId", localNodeId_}}, conn.authenticated);
            continue;
        }
        if (innerKind == "pong") {
            continue;
        }

        const QString peerAddr = conn.address.isEmpty() ? normalizeHostAddress(s->peerAddress()) : conn.address;
        const QString peerNodeId = conn.nodeId.isEmpty() ? obj.value("nodeId").toString() : conn.nodeId;
        emit messageReceived(obj, peerNodeId, s, peerAddr);
    }
}

void PeerNode::onSocketDisconnected() {
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s || !connections_.contains(s)) return;
    const QString nodeId = connections_[s].nodeId;
    const bool wasAuth = connections_[s].authenticated;
    connections_.remove(s);
    if (!nodeId.isEmpty() && nodeSockets_.value(nodeId) == s) {
        nodeSockets_.remove(nodeId);
        // Check if another active authenticated socket exists for this same nodeId
        for (auto it = connections_.begin(); it != connections_.end(); ++it) {
            if (it.value().nodeId == nodeId && it.value().authenticated &&
                it.key()->state() == QAbstractSocket::ConnectedState) {
                nodeSockets_.insert(nodeId, it.key());
                break;
            }
        }
        if (wasAuth && !nodeSockets_.contains(nodeId)) {
            emit peerDisconnected(nodeId);
        }
    }
    s->deleteLater();
}

void PeerNode::readDiscovery() {
    while (discovery_.hasPendingDatagrams()) {
        const auto datagram = discovery_.receiveDatagram();
        const auto doc = QJsonDocument::fromJson(datagram.data());
        if (!doc.isObject()) continue;
        const auto j = doc.object();
        if (j.value("kind").toString() != "discovery") continue;

        const QString hostNodeId = j.value("hostNodeId").toString();
        if (!localNodeId_.isEmpty() && hostNodeId == localNodeId_) {
            // Filter out our own UDP broadcast
            continue;
        }

        DiscoveredSession ds;
        ds.pandoraId = j.value("pandoraId").toString();
        ds.name = j.value("name").toString();
        ds.hostNodeId = hostNodeId;
        ds.hostName = j.value("hostName").toString();
        ds.address = normalizeHostAddress(datagram.senderAddress());
        ds.port = static_cast<quint16>(j.value("port").toInt(43170));
        ds.nodeCount = j.value("nodeCount").toInt(1);
        ds.lastSeen = QDateTime::currentDateTimeUtc();

        if (!ds.pandoraId.isEmpty() && ds.port > 0) {
            emit sessionDiscovered(ds);
        }
    }
}

void PeerNode::onHeartbeatTick() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto sockets = connections_.keys();
    for (auto* s : sockets) {
        if (!connections_.contains(s)) continue;
        auto& conn = connections_[s];
        const qint64 idleSecs = conn.lastActivityUtc.secsTo(now);
        if (idleSecs > 12) {
            // Peer timed out
            const QString nid = conn.nodeId;
            const bool wasAuth = conn.authenticated;
            connections_.remove(s);
            if (!nid.isEmpty() && nodeSockets_.value(nid) == s) {
                nodeSockets_.remove(nid);
                if (wasAuth) emit peerDisconnected(nid);
            }
            s->disconnect(this);
            s->abort();
            s->deleteLater();
            continue;
        }
        if (s->state() == QAbstractSocket::ConnectedState && conn.authenticated) {
            writeFrame(s, {{"kind", "ping"}, {"nodeId", localNodeId_}}, true);
        }
    }
}

void PeerNode::writeFrame(QTcpSocket* s, const QJsonObject& o, bool sealIfPossible) {
    if (!s || s->state() != QAbstractSocket::ConnectedState) return;
    QJsonObject outObj = o;
    if (sealIfPossible && !transportKey_.isEmpty() && connections_.contains(s) && connections_[s].authenticated) {
        const quint64 seq = ++connections_[s].sendSeq;
        outObj = SessionSecurity::sealJson(o, transportKey_, seq);
    }
    QByteArray bytes = QJsonDocument(outObj).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    s->write(bytes);
}

} // namespace pandora
