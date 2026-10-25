// Example: two in-process Na'vi exchanging events the way the integration glue will wire them.
// Replace the lambdas with Networking's broadcastMessage()/sendMessage() and messageReceived/peerDisconnected signals.

#include <QCoreApplication>
#include <QDebug>
#include <QUuid>

#include <pandora/metadata/MetadataService.h>

using namespace pandora;
using namespace pandora::shared;
using pandora::metadata::MetadataService;

static Node makeNode(const QString& name) {
    Node n;
    n.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // really: Security::generateNodeID()
    n.name = name;
    return n;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    MetadataService hiren(makeNode("Hiren"));
    MetadataService neytiri(makeNode("Neytiri"));
    hiren.open();
    neytiri.open();

    // "Network": deliver every outgoing message to the other side.
    auto wire = [](MetadataService& from, MetadataService& to) {
        QObject::connect(&from, &MetadataService::outgoingMessage, &to, &MetadataService::handleMessage);
    };
    wire(hiren, neytiri);
    wire(neytiri, hiren);
    QObject::connect(&hiren, &MetadataService::guardianChanged,
                     [](const QString& o, const QString& n) { qInfo() << "Guardian" << o << "->" << n; });

    // 1. Hiren creates Pandora (keyHash would come from Security::generatePandoraKey()).
    Pandora p;
    p.id = QStringLiteral("pandora-1");
    p.keyHash = QStringLiteral("hash-of-key");
    hiren.createPandora(p);

    // 2. Neytiri joins: after Security validated her key, the Guardian admits her.
    Message reply;
    hiren.admitNavi(neytiri.self(), &reply);
    neytiri.handleMessage(reply);  // in reality delivered by Networking.sendMessage()

    // 3. A file is uploaded on Neytiri's node; Storage already picked the replicas.
    FileMetadata f;
    f.fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    f.name = QStringLiteral("report.pdf");
    f.sizeBytes = 1'000'000;
    f.replicaNodeIds = QStringList{hiren.self().id, neytiri.self().id};
    neytiri.addMetadata(f);
    qInfo() << "Hiren sees files:" << hiren.listFiles().size() << "usage:" << hiren.storageUsageByNavi();

    // 4. The Guardian's audit log (hiren.shutdownPandora() would start the 2-minute countdown).
    qInfo().noquote() << "\n" << hiren.generateLogs();
    return 0;
}
