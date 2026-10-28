#include <pandora/networking/NetworkManager.h>
#include <QCoreApplication>
#include <QDebug>
using namespace pandora;
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);
 const auto args=app.arguments();
 if(args.size()!=4 && args.size()!=6) {
  qCritical()<<"Usage: pandora_peer sessionId name listenPort [peerIPv4 peerPort]";
  return 2;
 }
 bool valid=false;const auto port=args[3].toUInt(&valid);
 if(!valid || port>65535) return 2;
 NetworkManager manager;
 const Node local{QUuid::createUuid().toString(QUuid::WithoutBraces),args[2]};
 QObject::connect(&manager,&NetworkManager::networkError,[](const QString& e){qWarning()<<e;});
 QObject::connect(&manager,&NetworkManager::peerDiscovered,[](const Node& n){
  qInfo()<<"Discovered candidate:"<<n.id<<n.name<<n.address<<n.port;
 });
 QObject::connect(&manager,&NetworkManager::peerDisconnected,[](const QString& id){
  qInfo()<<"Disconnected:"<<id;
 });
 QObject::connect(&manager,&NetworkManager::messageReceived,[](const QString& id,const Message& m){
  qInfo()<<"Received from"<<id<<messageTypeName(m.type)<<m.payload;
 });
 QObject::connect(&manager,&NetworkManager::peerConnected,[&](const Node& n){
  qInfo()<<"Transport connected (not authenticated):"<<n.id;
  Message m;m.senderId=local.id;m.pandoraId=args[1];m.type=MessageType::CREATE_PANDORA;
  m.payload={{"demo",true}};
  if(!manager.sendMessage(n.id,m)) qWarning()<<"Demo enqueue rejected";
 });
 if(!manager.start(local,args[1],QHostAddress::AnyIPv4,quint16(port))) return 1;
 qInfo()<<"Listening on"<<manager.listeningPort()<<"temporary node"<<local.id;
 if(!manager.startDiscovery()) qWarning()<<"Discovery unavailable; explicit TCP still possible";
 if(args.size()==6) {
  const QHostAddress address(args[4]);bool ok=false;const auto remotePort=args[5].toUInt(&ok);
  if(!ok || remotePort==0 || remotePort>65535 ||
    !manager.connectToPeer(address,quint16(remotePort))) return 2;
 }
 QObject::connect(&app,&QCoreApplication::aboutToQuit,&manager,&NetworkManager::stop);
 return app.exec();
}

