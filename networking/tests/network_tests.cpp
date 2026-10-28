#include <QtTest>
#include <pandora/shared/Protocol.h>
#include <pandora/networking/NetworkManager.h>
using namespace pandora;
class Tests : public QObject {
 Q_OBJECT
private slots:
 void serialization() {
  for(int i=0;i<14;++i) {
   Message m;m.type=static_cast<MessageType>(i);m.senderId="node";m.pandoraId="session";
   m.payload={{"name",QString::fromUtf8("Na'vi 日本")}};
   auto d=Message::fromJson(m.toJson());QVERIFY(d);QCOMPARE(d->toJson(),m.toJson());
  }
  QVERIFY(!Message::fromJson({}));
 }
 void invalidEnvelopes() {
  Message m;m.senderId="n";m.pandoraId="s";
  auto o=m.toJson();o["protocol"]=2;QVERIFY(!Message::fromJson(o));
  o=m.toJson();o["type"]="UNKNOWN";QVERIFY(!Message::fromJson(o));
  o=m.toJson();o["payload"]=42;QVERIFY(!Message::fromJson(o));
  o=m.toJson();o["senderId"]="";QVERIFY(!Message::fromJson(o));
 }
 void oversizedFrame() {
  FrameDecoder d;QList<QJsonObject> out;QString error;
  QVERIFY(!d.feed(QByteArray::fromHex("00100001"),out,error));
  QVERIFY(encodeFrame({{"large",QString(MaxFrameBytes,'x')}}).isEmpty());
  FrameDecoder array;QVERIFY(!array.feed(QByteArray::fromHex("000000025b5d"),out,error));
 }
 void framing() {
  QJsonObject o{{"test",42}};auto bytes=encodeFrame(o);FrameDecoder d;
  QList<QJsonObject> out;QString error;
  for(char c:bytes) QVERIFY(d.feed(QByteArray(1,c),out,error));
  QCOMPARE(out.size(),1);QCOMPARE(out.first(),o);
  QVERIFY(d.feed(bytes+bytes,out,error));QCOMPARE(out.size(),3);
  FrameDecoder bad;QVERIFY(!bad.feed(QByteArray(4,'\0'),out,error));
  FrameDecoder malformed;QVERIFY(!malformed.feed(QByteArray::fromHex("000000017b"),out,error));
 }
 void transport() {
  NetworkManager a,b,c;Node na{"a","Alice"},nb{"b","Bob"},nc{"c","Carol"};
  QVERIFY(a.start(na,"session",QHostAddress::LocalHost));
  QVERIFY(b.start(nb,"session",QHostAddress::LocalHost));
  QVERIFY(c.start(nc,"session",QHostAddress::LocalHost));
  QSignalSpy receivedB(&b,&NetworkManager::messageReceived), receivedC(&c,&NetworkManager::messageReceived);
  QSignalSpy left(&a,&NetworkManager::peerDisconnected);
  QVERIFY(a.connectToPeer(QHostAddress::LocalHost,b.listeningPort()));
  QVERIFY(a.connectToPeer(QHostAddress::LocalHost,c.listeningPort()));
  QTRY_COMPARE(a.getConnectedPeers().size(),2);
  QTRY_COMPARE(b.getConnectedPeers().size(),1);
  QTRY_COMPARE(c.getConnectedPeers().size(),1);
  Message m;m.senderId="a";m.pandoraId="session";m.payload={{"hello","world"}};
  QVERIFY(a.sendMessage("b",m));QTRY_COMPARE(receivedB.count(),1);
  QCOMPARE(qvariant_cast<Message>(receivedB.first().at(1)).payload,m.payload);
  QCOMPARE(a.broadcastMessage(m),2);
  QTRY_COMPARE(receivedB.count(),2);QTRY_COMPARE(receivedC.count(),1);
  m.senderId="spoof";QVERIFY(!a.sendMessage("b",m));
  QVERIFY(a.disconnectPeer("b"));QCOMPARE(left.count(),1);
  QTRY_COMPARE(b.getConnectedPeers().size(),0);
  a.stop();b.stop();c.stop();QCOMPARE(a.getConnectedPeers().size(),0);
 }
 void discovery() {
  QUdpSocket reservation;QVERIFY(reservation.bind(QHostAddress::LocalHost,0));
  const auto port=reservation.localPort();reservation.close();
  NetworkManager a;QVERIFY(a.start({"a","A"},"session",QHostAddress::LocalHost));
  QSignalSpy discovered(&a,&NetworkManager::peerDiscovered);
  QVERIFY(a.startDiscovery(port,10000));
  QUdpSocket sender;
  QJsonObject ad{{"service","pandora-discovery"},{"protocol",ProtocolVersion},
   {"pandoraId","wrong"},{"nodeId","b"},{"name","B"},{"port",5555}};
  auto send=[&]{return sender.writeDatagram(QJsonDocument(ad).toJson(QJsonDocument::Compact),QHostAddress::LocalHost,port);};
  QVERIFY(send()>0);QTest::qWait(100);QCOMPARE(discovered.count(),0);
  ad["pandoraId"]="session";QVERIFY(send()>0);
  QTRY_COMPARE(discovered.count(),1);
  const auto peer=qvariant_cast<Node>(discovered.first().first());
  QCOMPARE(peer.id,QString("b"));QCOMPARE(peer.port,quint16(5555));
  a.stopDiscovery();a.stop();
 }
 void limitsAndRestart() {
  NetworkManager a;NetworkConfig cfg;cfg.maxNodes=1;
  QVERIFY(a.start({"a","A"},"session",QHostAddress::LocalHost,0,cfg));
  QVERIFY(!a.connectToPeer(QHostAddress::LocalHost,12345));
  QVERIFY(!a.start({"a","A"},"session"));
  a.stop();a.stop();
  QVERIFY(a.start({"a","A"},"session",QHostAddress::LocalHost));
  Message m;m.senderId="a";m.pandoraId="session";
  QVERIFY(!a.sendMessage("absent",m));QCOMPARE(a.broadcastMessage(m),0);
 }
 void mismatchAndTimeout() {
  NetworkManager a,b;NetworkConfig cfg;cfg.heartbeatMs=20;cfg.timeoutMs=100;
  QVERIFY(a.start({"a","Alice"},"one",QHostAddress::LocalHost,0,cfg));
  QVERIFY(b.start({"b","Bob"},"two",QHostAddress::LocalHost,0,cfg));
  QSignalSpy errors(&a,&NetworkManager::networkError);
  QVERIFY(a.connectToPeer(QHostAddress::LocalHost,b.listeningPort()));
  QTRY_VERIFY(errors.count()>0);QCOMPARE(a.getConnectedPeers().size(),0);
  QTcpSocket silent;silent.connectToHost(QHostAddress::LocalHost,a.listeningPort());
  QTRY_COMPARE(silent.state(),QAbstractSocket::ConnectedState);
  QTRY_COMPARE(silent.state(),QAbstractSocket::UnconnectedState);
 }
 void simultaneousDial() {
  NetworkManager a,b;
  QVERIFY(a.start({"a","A"},"session",QHostAddress::LocalHost));
  QVERIFY(b.start({"b","B"},"session",QHostAddress::LocalHost));
  QVERIFY(a.connectToPeer(QHostAddress::LocalHost,b.listeningPort()));
  QVERIFY(b.connectToPeer(QHostAddress::LocalHost,a.listeningPort()));
  QTRY_COMPARE(a.getConnectedPeers().size(),1);
  QTRY_COMPARE(b.getConnectedPeers().size(),1);
  QTest::qWait(100);
  QCOMPARE(a.getConnectedPeers().size(),1);QCOMPARE(b.getConnectedPeers().size(),1);
  Message m;m.senderId="a";m.pandoraId="session";
  QSignalSpy received(&b,&NetworkManager::messageReceived);
  QVERIFY(a.sendMessage("b",m));QTRY_COMPARE(received.count(),1);
 }
 void simultaneousDialAtCapacity() {
  NetworkManager a,b,c; NetworkConfig cfg; cfg.maxNodes=2;
  QVERIFY(a.start({"a","Alice"},"session",QHostAddress::LocalHost,0,cfg));
  QVERIFY(b.start({"b","Bob"},"session",QHostAddress::LocalHost,0,cfg));
  QVERIFY(c.start({"c","Carol"},"session",QHostAddress::LocalHost,0,cfg));
  QVERIFY(a.connectToPeer(QHostAddress::LocalHost,b.listeningPort()));
  QVERIFY(b.connectToPeer(QHostAddress::LocalHost,a.listeningPort()));
  QTRY_COMPARE_WITH_TIMEOUT(a.getConnectedPeers().size(),1,2000);
  QTRY_COMPARE_WITH_TIMEOUT(b.getConnectedPeers().size(),1,2000);
  QTest::qWait(100);
  QCOMPARE(a.getConnectedPeers().first().id,QString("b"));
  QCOMPARE(b.getConnectedPeers().first().id,QString("a"));
  QSignalSpy receivedA(&a,&NetworkManager::messageReceived),receivedB(&b,&NetworkManager::messageReceived);
  Message m; m.pandoraId="session"; m.senderId="a";
  QVERIFY(a.sendMessage("b",m)); QTRY_COMPARE(receivedB.count(),1);
  m.senderId="b";
  QVERIFY(b.sendMessage("a",m)); QTRY_COMPARE(receivedA.count(),1);
  // A spare handshake slot must never admit an extra distinct ready peer.
  QSignalSpy errors(&a,&NetworkManager::networkError);
  QVERIFY(c.connectToPeer(QHostAddress::LocalHost,a.listeningPort()));
  QTRY_VERIFY(errors.count()>0);
  QTRY_COMPARE(c.getConnectedPeers().size(),0);
  QCOMPARE(a.getConnectedPeers().size(),1);
  QCOMPARE(a.getConnectedPeers().first().id,QString("b"));
 }
 void handshakeCapacityBound() {
  NetworkManager manager; NetworkConfig cfg; cfg.maxNodes=2;
  QVERIFY(manager.start({"a","Alice"},"session",QHostAddress::LocalHost,0,cfg));
  QTcpSocket first,second,excess;
  first.connectToHost(QHostAddress::LocalHost,manager.listeningPort());
  second.connectToHost(QHostAddress::LocalHost,manager.listeningPort());
  QTRY_COMPARE(first.state(),QAbstractSocket::ConnectedState);
  QTRY_COMPARE(second.state(),QAbstractSocket::ConnectedState);
  // Wait for both listeners' HELLOs so both pending slots are occupied.
  QTRY_VERIFY(first.bytesAvailable()>0);
  QTRY_VERIFY(second.bytesAvailable()>0);
  QSignalSpy errors(&excess,&QTcpSocket::errorOccurred);
  excess.connectToHost(QHostAddress::LocalHost,manager.listeningPort());
  QTRY_VERIFY(errors.count()>0);
  QTRY_COMPARE(excess.state(),QAbstractSocket::UnconnectedState);
  QCOMPARE(first.state(),QAbstractSocket::ConnectedState);
  QCOMPARE(second.state(),QAbstractSocket::ConnectedState);
  QVERIFY(!manager.connectToPeer(QHostAddress::LocalHost,1));
  QCOMPARE(manager.getConnectedPeers().size(),0);
 }
 void framingValidPrefix_data() {
  QTest::addColumn<QByteArray>("bad");
  QTest::newRow("malformed-json") << QByteArray::fromHex("000000017b");
  QTest::newRow("zero-length") << QByteArray(4,'\0');
  QTest::newRow("oversize-length") << QByteArray::fromHex("00100001");
  QTest::newRow("json-array") << QByteArray::fromHex("000000025b5d");
 }
 void framingValidPrefix() {
  QFETCH(QByteArray,bad);
  const QJsonObject first{{"sequence",1}},second{{"sequence",2}},after{{"sequence",3}};
  const auto wire=encodeFrame(first)+encodeFrame(second)+bad+encodeFrame(after);
  // Every two-part split, including no split, must yield precisely the valid prefix.
  for(qsizetype split=0;split<=wire.size();++split) {
   FrameDecoder decoder; QList<QJsonObject> frames; QString error;
   bool ok=decoder.feed(wire.left(split),frames,error);
   if(ok) ok=decoder.feed(wire.mid(split),frames,error);
   QVERIFY(!ok); QVERIFY(!error.isEmpty());
   QCOMPARE(frames,(QList<QJsonObject>{first,second}));
  }
 }
 void validPrefixDelivery_data() {
  QTest::addColumn<bool>("split");
  QTest::addColumn<QByteArray>("bad");
  for(bool split:{false,true}) {
   const QByteArray prefix=split ? "split-" : "coalesced-";
   QTest::newRow((prefix+"malformed-json").constData()) << split << QByteArray::fromHex("000000017b");
   QTest::newRow((prefix+"zero-length").constData()) << split << QByteArray(4,'\0');
   QTest::newRow((prefix+"oversize-length").constData()) << split << QByteArray::fromHex("00100001");
   QTest::newRow((prefix+"json-array").constData()) << split << QByteArray::fromHex("000000025b5d");
  }
 }
 void validPrefixDelivery() {
  QFETCH(bool,split); QFETCH(QByteArray,bad);
  NetworkManager manager;
  QVERIFY(manager.start({"a","Alice"},"session",QHostAddress::LocalHost));
  QTcpSocket raw; raw.connectToHost(QHostAddress::LocalHost,manager.listeningPort());
  QTRY_COMPARE(raw.state(),QAbstractSocket::ConnectedState);
  const auto hello=encodeFrame({{"control","HELLO"},{"protocol",ProtocolVersion},
   {"pandoraId","session"},{"nodeId","raw"},{"name","Raw peer"},{"port",12345}});
  QCOMPARE(raw.write(hello),qint64(hello.size())); raw.flush();
  QTRY_COMPARE(manager.getConnectedPeers().size(),1);
  QSignalSpy received(&manager,&NetworkManager::messageReceived);
  QSignalSpy left(&manager,&NetworkManager::peerDisconnected);
  QSignalSpy errors(&manager,&NetworkManager::networkError);
  Message first; first.senderId="raw"; first.pandoraId="session"; first.payload={{"sequence",1}};
  Message second=first; second.id="second"; second.payload={{"sequence",2}};
  Message after=first; after.id="after-error"; after.payload={{"sequence",3}};
  const auto valid=encodeFrame(first.toJson())+encodeFrame(second.toJson());
  const auto suffix=bad+encodeFrame(after.toJson());
  const auto wire=split ? valid : valid+suffix;
  QCOMPARE(raw.write(wire),qint64(wire.size())); raw.flush();
  if(split) {
   QTRY_COMPARE(received.count(),2);
   QCOMPARE(raw.write(suffix),qint64(suffix.size())); raw.flush();
  }
  QTRY_COMPARE(left.count(),1);
  QCOMPARE(received.count(),2);
  QCOMPARE(qvariant_cast<Message>(received.at(0).at(1)).payload,first.payload);
  QCOMPARE(qvariant_cast<Message>(received.at(1).at(1)).payload,second.payload);
  QVERIFY(errors.count()>0);
  QCOMPARE(manager.getConnectedPeers().size(),0);
 }

};
QTEST_GUILESS_MAIN(Tests)
#include "network_tests.moc"



