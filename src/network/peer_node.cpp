#include "network/peer_node.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkDatagram>

namespace pandora {
PeerNode::PeerNode(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &PeerNode::acceptConnections);
    connect(&discovery_, &QUdpSocket::readyRead, this, &PeerNode::readDiscovery);
}
bool PeerNode::start(quint16 tcpPort, quint16 discoveryPort) {
    discoveryPort_ = discoveryPort;
    if (!server_.listen(QHostAddress::AnyIPv4, tcpPort)) { emit statusChanged(server_.errorString()); return false; }
    if (!discovery_.bind(QHostAddress::AnyIPv4, discoveryPort_, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        server_.close(); emit statusChanged(discovery_.errorString()); return false;
    }
    emit statusChanged(QString("Listening on TCP %1 / UDP %2").arg(server_.serverPort()).arg(discoveryPort_));
    return true;
}
void PeerNode::stop() {
    discovery_.close(); server_.close();
    for (auto* s : buffers_.keys()) { s->disconnect(this); s->abort(); s->deleteLater(); }
    buffers_.clear(); emit statusChanged("Offline");
}
quint16 PeerNode::port() const { return server_.serverPort(); }
void PeerNode::announce(const QJsonObject& payload) {
    QJsonObject j = payload; j["kind"] = "discovery";
    const auto bytes = QJsonDocument(j).toJson(QJsonDocument::Compact);
    discovery_.writeDatagram(bytes, QHostAddress::Broadcast, discoveryPort_);
}
void PeerNode::broadcastEvent(const PandoraEvent& e) {
    QJsonObject m{{"kind", "event"}, {"event", e.toJson()}};
    // Existing inbound sockets are used as a simple peer fanout. Production discovery needs
    // persistent outbound peer connections and acknowledgements.
    for (auto* socket : buffers_.keys()) writeJson(socket, m);
}
void PeerNode::sendTo(const QString& address, quint16 port, const QJsonObject& message) {
    auto* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, message]() { writeJson(socket, message); });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    socket->connectToHost(address, port);
}
void PeerNode::acceptConnections() {
    while (server_.hasPendingConnections()) {
        auto* s = server_.nextPendingConnection(); buffers_.insert(s, {});
        connect(s, &QTcpSocket::readyRead, this, &PeerNode::readSocket);
        connect(s, &QTcpSocket::disconnected, this, [this, s]() { buffers_.remove(s); s->deleteLater(); });
    }
}
void PeerNode::readSocket() {
    auto* s = qobject_cast<QTcpSocket*>(sender()); if (!s) return;
    auto buffer = buffers_.value(s); buffer.append(s->readAll());
    while (true) {
        const auto newline = buffer.indexOf('\n'); if (newline < 0) break;
        const auto line = buffer.left(newline); buffer.remove(0, newline + 1);
        const auto doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) emit messageReceived(doc.object(), s->peerAddress().toString());
    }
    buffers_[s] = buffer;
}
void PeerNode::readDiscovery() {
    while (discovery_.hasPendingDatagrams()) {
        const auto datagram = discovery_.receiveDatagram();
        const auto doc = QJsonDocument::fromJson(datagram.data());
        if (!doc.isObject()) continue;
        const auto j = doc.object();
        if (j.value("kind").toString() == "discovery")
            emit peerDiscovered(datagram.senderAddress().toString(),
                                static_cast<quint16>(j.value("port").toInt()),
                                j.value("pandoraId").toString());
    }
}
void PeerNode::writeJson(QTcpSocket* s, const QJsonObject& o) {
    auto bytes = QJsonDocument(o).toJson(QJsonDocument::Compact); bytes.append('\n'); s->write(bytes);
}
}
