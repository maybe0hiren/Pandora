#include <pandora/networking/NetworkManager.h>
#include <QThread>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QLoggingCategory>

// TEMPORARY: disable with QT_LOGGING_RULES="pandora.networking.diagnostics.info=false".
Q_LOGGING_CATEGORY(pandoraNetworkDiagnostics, "pandora.networking.diagnostics")
namespace {
// Only public envelope/HELLO fields; never payloads, keys, credentials or unknown fields.
QJsonObject diagnosticFields(const QJsonObject& frame) {
 QJsonObject fields;
 for(const auto* key:{"control","protocol","pandoraId","nodeId","name","port","type","id","senderId"}) {
  if(!frame.contains(key)) continue;
  auto value=frame.value(key);
  if(value.isObject() || value.isArray()) value=QStringLiteral("<structured value omitted>");
  else if(value.isString() && value.toString().size()>160)
   value=value.toString().left(160)+QStringLiteral("<truncated>");
  fields.insert(key,value);
 }
 return fields;
}
}


namespace pandora {
NetworkManager::NetworkManager(QObject* parent):QObject(parent) {
 qRegisterMetaType<Node>(); qRegisterMetaType<Message>();
 // QObject member children follow this manager if moved before start().
 server_.setParent(this); discovery_.setParent(this);
 timer_.setParent(this); discoveryTimer_.setParent(this);
 connect(&server_,&QTcpServer::newConnection,this,[this] {
  while(server_.hasPendingConnections()) {
   auto* socket=server_.nextPendingConnection();
   // Reserve one additional bounded stream per remote slot for opposite dials.
   // The distinct ready-peer limit is enforced only after HELLO identifies duplicates.
   if(int(connections_.size())>=2*(config_.maxNodes-1)) {
    diagnostic(socket,"REJECT handshake socket capacity exceeded",
     {{"connections",int(connections_.size())},{"maxSockets",2*(config_.maxNodes-1)}});
    socket->abort();socket->deleteLater();
   }
   else attach(socket,false);
  }
 });
 connect(&timer_,&QTimer::timeout,this,&NetworkManager::tick);
}
NetworkManager::~NetworkManager() { stop(); }
void NetworkManager::assertThread() const { Q_ASSERT(QThread::currentThread()==thread()); }
bool NetworkManager::start(const Node& local,const QString& session,const QHostAddress& address,
 quint16 port,const NetworkConfig& config) {
 assertThread();
 if(server_.isListening() || local.id.isEmpty() || local.id.size()>128 ||
 local.name.size()>128 || session.isEmpty() || session.size()>128 ||
 config.maxNodes<1 || config.maxNodes>1000 || config.heartbeatMs<10 ||
 config.timeoutMs<config.heartbeatMs*2LL || config.maxQueuedBytes<MaxFrameBytes+4) {
  emit networkError("Invalid configuration or already started"); return false;
 }
 if(!server_.listen(address,port)) {emit networkError(server_.errorString());return false;}
 local_=local;local_.port=server_.serverPort();session_=session;config_=config;
 clock_.start();timer_.start(config_.heartbeatMs);return true;
}
void NetworkManager::stop() {
 assertThread();timer_.stop();stopDiscovery();server_.close();
 while(!connections_.empty()) drop(connections_.begin()->first,"manager stop");
 local_={};session_.clear();
}
quint16 NetworkManager::listeningPort() const {assertThread();return server_.serverPort();}
bool NetworkManager::connectToPeer(const QHostAddress& address,quint16 port) {
 assertThread();
 int pendingOutgoing=0;
 for(const auto& [socket,connection]:connections_)
  if(connection->outgoing && !connection->ready) ++pendingOutgoing;
 const int remoteLimit=config_.maxNodes-1;
 if(!server_.isListening() || address.isNull() || !port ||
 int(connections_.size())>=2*remoteLimit ||
 getConnectedPeers().size()+pendingOutgoing>=remoteLimit) return false;
 auto* socket=new QTcpSocket(this);
 attach(socket,true);
 diagnostic(socket,"CONNECT requested",{{"targetAddress",address.toString()},{"targetPort",port}});
 socket->connectToHost(address,port);return true;
}
void NetworkManager::attach(QTcpSocket* socket,bool outgoing) {
 socket->setParent(this);socket->setReadBufferSize(MaxFrameBytes+4);
 auto c=std::make_unique<Connection>();c->outgoing=outgoing;
 c->lastSeen=c->openedAt=clock_.elapsed();connections_.emplace(socket,std::move(c));
 diagnostic(socket,"ATTACH",{{"outgoing",outgoing}});
 auto hello=[this,socket] {
  const QJsonObject frame{{"control","HELLO"},{"protocol",ProtocolVersion},
    {"pandoraId",session_},{"nodeId",local_.id},{"name",local_.name},{"port",local_.port}};
  diagnostic(socket,"TX HELLO",diagnosticFields(frame));
  if(!queue(socket,frame)) drop(socket,"HELLO enqueue failure");
 };
 connect(socket,&QTcpSocket::connected,this,hello);
 connect(socket,&QTcpSocket::readyRead,this,[this,socket]{receive(socket);});
 connect(socket,&QTcpSocket::disconnected,this,[this,socket]{drop(socket,"socket disconnected signal");});
 connect(socket,&QTcpSocket::errorOccurred,this,[this,socket](QAbstractSocket::SocketError error) {
  diagnostic(socket,"SOCKET ERROR",{{"errorCode",int(error)},{"errorString",socket->errorString()}});
  emit networkError(socket->errorString());drop(socket,"socket error");
 });
 if(!outgoing) hello();
}
bool NetworkManager::queue(QTcpSocket* socket,const QJsonObject& o) {
 if(!connections_.contains(socket) || socket->state()!=QAbstractSocket::ConnectedState) {
  diagnostic(socket,"ENQUEUE rejected: untracked or not ConnectedState");return false;
 }
 auto frame=encodeFrame(o);
 if(frame.isEmpty() || socket->bytesToWrite()+frame.size()>config_.maxQueuedBytes) {
  diagnostic(socket,"ENQUEUE rejected: empty/oversize frame or output limit",
   {{"frameBytes",qint64(frame.size())},{"queuedBytes",socket->bytesToWrite()},
    {"maxQueuedBytes",config_.maxQueuedBytes}});
  return false;
 }
 if(socket->write(frame)!=frame.size()) {emit networkError("Socket enqueue failed");drop(socket,"socket write did not accept complete frame");return false;}
 return true;
}
void NetworkManager::receive(QTcpSocket* socket) {
 while(connections_.contains(socket) && socket->bytesAvailable()) {
  QList<QJsonObject> frames;QString error;
  const bool decoded=connections_.at(socket)->decoder.feed(socket->read(16384),frames,error);
  // A failed feed can still return complete valid frames before the bad frame.
  // Dispatch that prefix before closing, exactly as if it arrived in separate reads.
  for(const auto& frame:frames) {
   if(!connections_.contains(socket)) return;
   process(socket,frame);
  }
  if(!decoded && connections_.contains(socket)) {
   emit networkError(error);drop(socket,"frame decoding failed: "+error);return;
  }
 }
}
QTcpSocket* NetworkManager::socketFor(const QString& id) const {
 for(const auto& [s,c]:connections_) if(c->ready && c->peer.id==id) return s;
 return nullptr;
}
void NetworkManager::process(QTcpSocket* socket,const QJsonObject& o) {
 auto& c=*connections_.at(socket);
 diagnostic(socket,"RX FRAME (payload/unknown fields omitted)",diagnosticFields(o));
 const auto control=o.value("control").toString();
 auto reject=[this,socket](const QString& reason) {
  emit networkError("Invalid transport frame: "+reason);drop(socket,reason);
 };
 if(!c.ready) {
  const auto id=o.value("nodeId").toString();
  const auto port=o.value("port").toDouble(-1);
  // Same predicates, ordering and conversions as the original combined check.
  QString failure;
  if(control!="HELLO") failure="HELLO control != HELLO";
  else if(o.value("protocol").toDouble(-1)!=ProtocolVersion) failure="HELLO protocol != ProtocolVersion";
  else if(o.value("pandoraId").toString()!=session_) failure="HELLO pandoraId != local session";
  else if(id.isEmpty()) failure="HELLO nodeId is empty (or not a string)";
  else if(id.size()>128) failure="HELLO nodeId length > 128";
  else if(id==local_.id) failure="HELLO nodeId == local nodeId (self connection)";
  else if(!o.value("name").isString()) failure="HELLO name is not a string";
  else if(o.value("name").toString().size()>128) failure="HELLO name length > 128";
  else if(port<1) failure="HELLO port < 1 (missing/non-number converts to -1)";
  else if(port>65535) failure="HELLO port > 65535";
  else if(port!=int(port)) failure="HELLO port is not an integer";
  if(!failure.isEmpty()) {
   diagnostic(socket,"REJECT "+failure,{{"actual",diagnosticFields(o)},
    {"expectedControl","HELLO"},{"expectedProtocol",ProtocolVersion},
    {"expectedPandoraId",session_},{"localNodeId",local_.id},
    {"nodeIdLength",id.size()},{"nameLength",o.value("name").toString().size()},
    {"convertedPort",port},{"minimumPort",1},{"maximumPort",65535},{"maximumIdentityLength",128}});
   reject(failure);return;
  }
  if(auto* existing=socketFor(id)) {
   // Both ends retain the connection initiated by the lexically smaller ID.
   const bool preferOutgoing=local_.id<id;
   diagnostic(socket,"DUPLICATE selection",{{"peerId",id},{"preferOutgoing",preferOutgoing},
    {"candidateOutgoing",c.outgoing},{"existingOutgoing",connections_.at(existing)->outgoing},
    {"existingSocket",QString::number(quintptr(existing),16)}});
   if(c.outgoing!=preferOutgoing || connections_.at(existing)->outgoing==preferOutgoing) {
    drop(socket,"duplicate: reject candidate (direction not preferred or existing direction preferred)");return;
   }
   drop(existing,"duplicate: replace existing with preferred candidate");
   if(!connections_.contains(socket)) return;
  }
  if(getConnectedPeers().size()>=config_.maxNodes-1) {
   reject("ready peer capacity exceeded");return;
  }
  c.peer={id,o.value("name").toString(),socket->peerAddress(),quint16(port)};
  c.ready=true;c.lastSeen=clock_.elapsed();diagnostic(socket,"HELLO accepted");const Node snapshot=c.peer;emit peerConnected(snapshot);return;
 }
 if(control=="PING" || control=="PONG") {
  c.lastSeen=clock_.elapsed();
  if(control=="PING" && !queue(socket,{{"control","PONG"}})) drop(socket,"PONG enqueue failure");
  return;
 }
 if(!control.isEmpty()) {reject("nonempty control on already-ready connection (expected PING/PONG or application envelope)");return;}
 QString error;auto message=Message::fromJson(o,&error);
 if(!message || message->senderId!=c.peer.id || message->pandoraId!=session_) {
  diagnostic(socket,"REJECT application envelope",{{"actual",diagnosticFields(o)},
   {"expectedSenderId",c.peer.id},{"expectedPandoraId",session_},{"parseError",error}});
  reject(!message ? "application envelope parsing failed: "+error :
   (message->senderId!=c.peer.id ? "application senderId != HELLO peer ID" : "application pandoraId != local session"));
  return;
 }
 c.lastSeen=clock_.elapsed();const QString peerId=c.peer.id;emit messageReceived(peerId,*message);
}
void NetworkManager::diagnostic(QTcpSocket* socket,const QString& event,const QJsonObject& fields) const {
 const auto it=connections_.find(socket);
 qCInfo(pandoraNetworkDiagnostics).noquote()
  << event << "node=" << local_.id << "socket=" << QString::number(quintptr(socket),16)
  << "direction=" << (it==connections_.end() ? "untracked" : (it->second->outgoing ? "outgoing" : "incoming"))
  << "ready=" << (it!=connections_.end() && it->second->ready)
  << "local=" << socket->localAddress().toString() << socket->localPort()
  << "remote=" << socket->peerAddress().toString() << socket->peerPort()
  << "state=" << int(socket->state()) << QJsonDocument(fields).toJson(QJsonDocument::Compact);
}
void NetworkManager::drop(QTcpSocket* socket,const QString& reason) {
 auto it=connections_.find(socket);
 diagnostic(socket,"DROP "+reason,{{"tracked",it!=connections_.end()}});
 if(it==connections_.end()) return;
 const QString id=it->second->ready?it->second->peer.id:QString{};
 connections_.erase(it);socket->disconnect(this);socket->abort();socket->deleteLater();
 if(!id.isEmpty()) emit peerDisconnected(id);
}
bool NetworkManager::disconnectPeer(const QString& id) {
 assertThread();auto* socket=socketFor(id);if(!socket) return false;drop(socket,"disconnectPeer requested");return true;
}
bool NetworkManager::sendMessage(const QString& id,const Message& message) {
 assertThread();QString error;
 if(message.senderId!=local_.id || message.pandoraId!=session_ ||
 !Message::fromJson(message.toJson(),&error)) return false;
 auto* socket=socketFor(id);return socket && queue(socket,message.toJson());
}
int NetworkManager::broadcastMessage(const Message& message) {
 assertThread();int sent=0;const auto peers=getConnectedPeers();
 for(const auto& peer:peers) if(sendMessage(peer.id,message)) ++sent;
 return sent;
}
QList<Node> NetworkManager::getConnectedPeers() const {
 assertThread();QList<Node> peers;
 for(const auto& [s,c]:connections_) if(c->ready) peers.append(c->peer);
 return peers;
}
void NetworkManager::tick() {
 QList<QTcpSocket*> sockets;
 for(const auto& [s,c]:connections_) sockets.append(s);
 for(auto* s:sockets) {
  if(!connections_.contains(s)) continue;
  const auto& c=*connections_.at(s);
  if(clock_.elapsed()-(c.ready?c.lastSeen:c.openedAt)>config_.timeoutMs) {
   diagnostic(s,c.ready ? "HEARTBEAT timeout" : "HANDSHAKE timeout",
    {{"elapsedMs",clock_.elapsed()-(c.ready?c.lastSeen:c.openedAt)},{"timeoutMs",config_.timeoutMs}});
   const QString reason=c.ready ? "heartbeat timeout" : "handshake timeout";
   emit networkError("Peer heartbeat or handshake timed out");drop(s,reason);
  } else if(c.ready && !queue(s,{{"control","PING"}})) drop(s,"PING enqueue failure");
 }
}
bool NetworkManager::startDiscovery(quint16 port,int intervalMs) {
 assertThread();
 if(!server_.isListening() || !port || intervalMs<100) return false;
 stopDiscovery();
 if(!discovery_.bind(QHostAddress::AnyIPv4,port,
   QUdpSocket::ShareAddress|QUdpSocket::ReuseAddressHint)) {
  emit networkError(discovery_.errorString());return false;
 }
 discoveryPort_=port;
 connect(&discovery_,&QUdpSocket::readyRead,this,[this] {
  while(discovery_.hasPendingDatagrams()) {
   const auto datagram=discovery_.receiveDatagram(2048);
   if(!datagram.isValid() || datagram.data().size()>1024) continue;
   QJsonParseError error;const auto doc=QJsonDocument::fromJson(datagram.data(),&error);
   if(error.error!=QJsonParseError::NoError || !doc.isObject()) continue;
   const auto o=doc.object();const QString id=o.value("nodeId").toString();
   const double port=o.value("port").toDouble(-1);
   if(o.value("service").toString()!="pandora-discovery" ||
    o.value("protocol").toDouble(-1)!=ProtocolVersion ||
    o.value("pandoraId").toString()!=session_ || id.isEmpty() ||
    id.size()>128 || id==local_.id || !o.value("name").isString() ||
    o.value("name").toString().size()>128 || port<1 || port>65535 || port!=int(port)) continue;
   const Node candidate{id,o.value("name").toString(),datagram.senderAddress(),quint16(port)};
   emit peerDiscovered(candidate);
   // A receiving slot may stop discovery or this session.
   if(!discoveryPort_) return;
  }
 });
 connect(&discoveryTimer_,&QTimer::timeout,this,&NetworkManager::advertise);
 discoveryTimer_.start(intervalMs);advertise();return true;
}
void NetworkManager::stopDiscovery() {
 assertThread();discoveryTimer_.stop();discovery_.close();discoveryPort_=0;
 discovery_.disconnect(this);discoveryTimer_.disconnect(this);
}
void NetworkManager::advertise() {
 if(!discoveryPort_) return;
 const auto bytes=QJsonDocument(QJsonObject{
  {"service","pandora-discovery"},{"protocol",ProtocolVersion},{"pandoraId",session_},
  {"nodeId",local_.id},{"name",local_.name},{"port",local_.port}
 }).toJson(QJsonDocument::Compact);
 QSet<QHostAddress> targets{QHostAddress::Broadcast};
 for(const auto& iface:QNetworkInterface::allInterfaces()) {
  if(!(iface.flags()&QNetworkInterface::IsUp) || !(iface.flags()&QNetworkInterface::CanBroadcast)) continue;
  for(const auto& entry:iface.addressEntries())
   if(entry.ip().protocol()==QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull())
    targets.insert(entry.broadcast());
 }
 for(const auto& address:targets)
  if(discovery_.writeDatagram(bytes,address,discoveryPort_)<0)
   emit networkError(discovery_.errorString());
}
}





