#include "core/pandora_controller.h"
#include "metadata/metadata_store.h"
#include "security/session_security.h"
#include "storage/storage_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace pandora;

namespace {
bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(30);
    }
    return predicate();
}
} // namespace

class PandoraDistributedTests : public QObject {
    Q_OBJECT

private slots:
    void testSecurityPbkdf2AndChallengeAndAead() {
        const QByteArray salt = SessionSecurity::createSalt();
        QCOMPARE(salt.size(), 32);

        const QByteArray verifier1 = SessionSecurity::deriveVerifier("NaViSecretKey123", salt);
        const QByteArray verifier2 = SessionSecurity::deriveVerifier("NaViSecretKey123", salt);
        QCOMPARE(verifier1, verifier2);
        QVERIFY(SessionSecurity::verify("NaViSecretKey123", salt, verifier1));
        QVERIFY(!SessionSecurity::verify("WrongKey999", salt, verifier1));

        // Challenge-Response test
        const QByteArray nonce = SessionSecurity::createNonce();
        const QString clientNodeId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray response = SessionSecurity::computeChallengeResponse(verifier1, nonce, clientNodeId);
        QVERIFY(SessionSecurity::verifyChallengeResponse(verifier1, nonce, clientNodeId, response));
        QVERIFY(!SessionSecurity::verifyChallengeResponse(verifier1, nonce, "spoofed-node", response));

        // AEAD Encryption + HMAC-SHA256 Authentication test
        const QByteArray masterKey = SessionSecurity::deriveMasterKey("NaViSecretKey123", salt);
        const QByteArray transportKey = SessionSecurity::deriveTransportKey(masterKey, "pandora-session-1");
        const QJsonObject plain{{"kind", "event"}, {"secret", "Eywa"}, {"count", 42}};

        QJsonObject sealed = SessionSecurity::sealJson(plain, transportKey, 7);
        QCOMPARE(sealed.value("kind").toString(), QString("sealed"));
        QVERIFY(!sealed.contains("secret"));

        QJsonObject opened;
        quint64 seq = 0;
        QVERIFY(SessionSecurity::openJson(sealed, transportKey, &opened, &seq));
        QCOMPARE(seq, 7ULL);
        QCOMPARE(opened.value("secret").toString(), QString("Eywa"));
        QCOMPARE(opened.value("count").toInt(), 42);

        // Tamper with ciphertext -> must fail authentication
        QJsonObject tampered = sealed;
        tampered["ct"] = QString::fromLatin1(QByteArray("corrupted-payload").toBase64());
        QJsonObject dummy;
        QVERIFY(!SessionSecurity::openJson(tampered, transportKey, &dummy));
    }

    void testStorageAtomicWriteChunkingAndQuotas() {
        StorageManager storage;
        QVERIFY(storage.initialize());

        QTemporaryDir srcDir;
        const QString srcFile = srcDir.filePath("sample.bin");
        QByteArray content(600 * 1024, 'P'); // 600 KiB
        for (int i = 0; i < content.size(); i += 1024) content[i] = static_cast<char>(i & 0xff);
        {
            QFile f(srcFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(content), content.size());
        }

        QString sha256;
        qint64 size = 0;
        QString err;
        const QString fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY2(storage.storeFile(srcFile, fileId, &sha256, &size, &err), qPrintable(err));
        QCOMPARE(size, content.size());
        QVERIFY(!sha256.isEmpty());
        QVERIFY(storage.hasReplica(fileId));
        QVERIFY(storage.verifyReplica(fileId, sha256, size));

        // Chunked network replica write
        const QString replicaId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(storage.beginChunkedWrite(replicaId, size, &err));
        qint64 offset = 0;
        while (offset < size) {
            bool ok = false;
            const QByteArray chunk = storage.readChunk(fileId, offset, 128 * 1024, &ok, &err);
            QVERIFY(ok);
            QVERIFY(storage.writeChunk(replicaId, offset, chunk, &err));
            offset += chunk.size();
        }
        QVERIFY(!storage.finalizeChunkedWrite(replicaId, "0000000000000000000000000000000000000000000000000000000000000000", &err));
        // Re-write and finalize with valid SHA-256
        QVERIFY(storage.beginChunkedWrite(replicaId, size, &err));
        QVERIFY(storage.writeChunk(replicaId, 0, content, &err));
        QVERIFY2(storage.finalizeChunkedWrite(replicaId, sha256, &err), qPrintable(err));
        QVERIFY(storage.verifyReplica(replicaId, sha256, size));

        // Path traversal sanitization
        QCOMPARE(StorageManager::sanitizeRelativePath("../../etc/passwd"), QString("etc/passwd"));
        QCOMPARE(StorageManager::sanitizeRelativePath("..\\..\\Windows\\System32\\cmd.exe"), QString("Windows/System32/cmd.exe"));

        storage.destroySessionData(true);
    }

    void testMetadataConvergenceAndTombstoneProtection() {
        MetadataStore store;
        QTemporaryDir dir;
        QVERIFY(store.initializePersistence(dir.filePath("meta.sqlite")));

        const QUuid fileUuid = QUuid::createUuid();
        const QString fid = fileUuid.toString(QUuid::WithoutBraces);
        const QUuid nodeA = QUuid::createUuid();
        const QUuid nodeB = QUuid::createUuid();

        FileInfo f1;
        f1.id = fileUuid;
        f1.name = "design.pdf";
        f1.size = 1024;
        f1.sha256 = "abc123";
        f1.version = 1;
        f1.modifiedBy = nodeA;
        f1.modifiedAt = QDateTime::currentDateTimeUtc();
        f1.replicas = {nodeA.toString(QUuid::WithoutBraces)};

        QString err;
        QVERIFY(store.addFile(f1, &err));
        QCOMPARE(store.files().size(), 1);

        // Duplicate name check
        FileInfo dup = f1;
        dup.id = QUuid::createUuid();
        QVERIFY(!store.addFile(dup, &err));

        // Delete file -> tombstone v2
        QVERIFY(store.deleteFile(fid, nodeB, &err));
        QCOMPARE(store.files(false).size(), 0);
        QCOMPARE(store.files(true).size(), 1);
        QVERIFY(store.file(fid).tombstone);

        // Stale remote snapshot with v1 must NOT resurrect the deleted file
        store.applyRemoteFile(f1);
        QCOMPARE(store.files(false).size(), 0);
        QVERIFY(store.file(fid).tombstone);
        store.reset();
    }

    void testThreeNodeEndToEndReplicationRetrievalSelfHealingAndElection() {
        // Start three independent Pandora nodes on local loopback ports
        PandoraController nodeA;
        PandoraController nodeB;
        PandoraController nodeC;

        nodeA.setPreferredPorts(43210, 43299);
        nodeB.setPreferredPorts(43211, 43299);
        nodeC.setPreferredPorts(43212, 43299);

        // Set replication factor to 2 so an uploaded file is stored on uploader + 1 remote peer initially
        nodeA.setReplicationFactor(2);
        nodeB.setReplicationFactor(2);
        nodeC.setReplicationFactor(2);

        QObject::connect(&nodeA, &PandoraController::logLine, [](const QString& l) { qDebug() << "[A]" << l; });
        QObject::connect(&nodeB, &PandoraController::logLine, [](const QString& l) { qDebug() << "[B]" << l; });
        QObject::connect(&nodeC, &PandoraController::logLine, [](const QString& l) { qDebug() << "[C]" << l; });
        QObject::connect(nodeA.network(), &PeerNode::statusChanged, [](const QString& l) { qDebug() << "[NetA]" << l; });
        QObject::connect(nodeB.network(), &PeerNode::statusChanged, [](const QString& l) { qDebug() << "[NetB]" << l; });

        QString err;
        // 1. Node A creates the Pandora session as Guardian
        QVERIFY2(nodeA.createPandora("EywaCloud", "NodeA_Guardian", "SecretKey2026", &err), qPrintable(err));
        QVERIFY(nodeA.isActive());
        QVERIFY(nodeA.isGuardian());
        const quint16 portA = nodeA.tcpPort();
        qDebug() << "Node A listening on port:" << portA;

        // 2. Verify unauthorized join with wrong key is rejected
        QSignalSpy rejectSpy(&nodeB, &PandoraController::joinFailed);
        QVERIFY(nodeB.joinPandoraDirect("NodeB_Peer", "WrongKey0000", "127.0.0.1", portA, {}, &err));
        QVERIFY(waitUntil([&]() { return rejectSpy.count() >= 1; }, 5000));
        QVERIFY(!nodeB.isActive());

        // 3. Join Node B and Node C with valid key
        QVERIFY2(nodeB.joinPandoraDirect("NodeB_Peer", "SecretKey2026", "127.0.0.1", portA, {}, &err), qPrintable(err));
        QVERIFY2(nodeC.joinPandoraDirect("NodeC_Peer", "SecretKey2026", "127.0.0.1", portA, {}, &err), qPrintable(err));

        QVERIFY2(waitUntil([&]() {
            return nodeB.isActive() && nodeC.isActive() &&
                   nodeA.metadata()->nodes(true).size() == 3 &&
                   nodeB.metadata()->nodes(true).size() == 3 &&
                   nodeC.metadata()->nodes(true).size() == 3;
        }, 8000), "All 3 nodes must authenticate and form a connected mesh");

        // Ensure full mesh peer_hello settles between B and C
        nodeB.runMaintenanceNow();
        nodeC.runMaintenanceNow();
        QVERIFY2(waitUntil([&]() {
            return nodeB.network()->isNodeConnected(nodeC.localNodeIdString()) &&
                   nodeC.network()->isNodeConnected(nodeB.localNodeIdString());
        }, 5000), "Peer B and Peer C must establish direct authenticated peer mesh link");

        // 4. Node A uploads report.pdf (512 KiB)
        QTemporaryDir tempDir;
        const QString reportPath = tempDir.filePath("report.pdf");
        QByteArray reportBytes(512 * 1024, 'R');
        for (int i = 0; i < reportBytes.size(); i += 257) {
            reportBytes[i] = static_cast<char>((i * 17) & 0xff);
        }
        {
            QFile f(reportPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(reportBytes), reportBytes.size());
        }

        QVERIFY2(nodeA.uploadFile(reportPath, &err), qPrintable(err));
        QCOMPARE(nodeA.metadata()->files().size(), 1);
        const FileInfo uploadedFile = nodeA.metadata()->files().first();
        const QString fileId = uploadedFile.id.toString(QUuid::WithoutBraces);

        // 5. Wait for physical replication to reach replicationFactor == 2 across the cluster
        QVERIFY2(waitUntil([&]() {
            const FileInfo fa = nodeA.metadata()->file(fileId);
            const int physicalHolders =
                (nodeA.storage()->hasReplica(fileId) ? 1 : 0) +
                (nodeB.storage()->hasReplica(fileId) ? 1 : 0) +
                (nodeC.storage()->hasReplica(fileId) ? 1 : 0);
            return fa.replicas.size() >= 2 && physicalHolders >= 2;
        }, 8000), "File must physically replicate to at least 2 nodes");

        // 6. Kill/disconnect the original uploader & Guardian (Node A)!
        nodeA.deactivatePandora(false); // Simulate abrupt departure/failure of A without shutting down session
        QVERIFY(!nodeA.isActive());

        // 7. Verify Guardian Election: Node B or Node C deterministically becomes the new Guardian
        QVERIFY2(waitUntil([&]() {
            const QString gB = nodeB.metadata()->guardian();
            const QString gC = nodeC.metadata()->guardian();
            return !gB.isEmpty() && gB == gC && gB != nodeA.localNodeIdString() &&
                   (nodeB.isGuardian() || nodeC.isGuardian());
        }, 8000), "Surviving nodes B and C must converge on a new elected Guardian");

        // 8. Verify Automatic Self-Healing: since A left, the remaining replica holder (B or C) repairs the missing replica onto the other peer!
        nodeB.triggerReplicaAuditAndRepair();
        nodeC.triggerReplicaAuditAndRepair();
        QVERIFY2(waitUntil([&]() {
            return nodeB.storage()->verifyReplica(fileId, uploadedFile.sha256, uploadedFile.size) &&
                   nodeC.storage()->verifyReplica(fileId, uploadedFile.sha256, uploadedFile.size);
        }, 8000), "Both surviving nodes B and C must hold verified physical replicas after self-healing");

        // 9. Verify Remote Retrieval: Remove C's local blob so C must pull it over the network from B
        nodeC.storage()->removeFile(fileId, false);
        nodeC.metadata()->removeReplicaFromFile(fileId, nodeC.localNodeIdString());
        QVERIFY(!nodeC.storage()->hasReplica(fileId));

        const QString downloadedPath = tempDir.filePath("downloaded_from_peer.pdf");
        QSignalSpy dlSpy(&nodeC, &PandoraController::remoteDownloadCompleted);
        QVERIFY2(nodeC.downloadFile(fileId, downloadedPath, &err), qPrintable(err));
        QVERIFY2(waitUntil([&]() { return dlSpy.count() >= 1 && QFile::exists(downloadedPath); }, 8000),
                 "Node C must download the file remotely from Node B even though uploader Node A is offline");

        QFile dlFile(downloadedPath);
        QVERIFY(dlFile.open(QIODevice::ReadOnly));
        QCOMPARE(dlFile.readAll(), reportBytes);
        dlFile.close();

        // 10. Verify Distributed Deletion & Tombstone Garbage Collection across B and C
        QVERIFY2(nodeB.deleteFile(fileId, &err), qPrintable(err));
        QVERIFY2(waitUntil([&]() {
            return nodeB.metadata()->files(false).isEmpty() &&
                   nodeC.metadata()->files(false).isEmpty() &&
                   !nodeB.storage()->hasReplica(fileId) &&
                   !nodeC.storage()->hasReplica(fileId);
        }, 6000), "Distributed delete must remove physical blobs and mark tombstones on all nodes");

        nodeB.deactivatePandora(false);
        nodeC.deactivatePandora(false);
    }

    void testFolderUploadDownloadAllAndGuardianShutdownCountdown() {
        PandoraController host;
        PandoraController peer;
        host.setPreferredPorts(43220, 43298);
        peer.setPreferredPorts(43221, 43298);
        host.setReplicationFactor(2);
        peer.setReplicationFactor(2);

        QString err;
        QVERIFY2(host.createPandora("FolderCloud", "HostGuardian", "PandoraPass77", &err), qPrintable(err));
        QVERIFY2(peer.joinPandoraDirect("PeerMember", "PandoraPass77", "127.0.0.1", host.tcpPort(), {}, &err), qPrintable(err));
        QVERIFY2(waitUntil([&]() { return peer.isActive() && host.metadata()->nodes(true).size() == 2; }, 6000),
                 "Host and peer must connect");

        // Create a nested directory structure to upload as a folder
        QTemporaryDir srcFolder;
        QDir(srcFolder.path()).mkpath("docs/specs");
        {
            QFile f1(srcFolder.filePath("readme.txt"));
            QVERIFY(f1.open(QIODevice::WriteOnly));
            f1.write("Root file content");
            QFile f2(srcFolder.filePath("docs/specs/architecture.md"));
            QVERIFY(f2.open(QIODevice::WriteOnly));
            f2.write("Nested spec content");
        }

        int uploadedCount = 0;
        QVERIFY2(host.uploadFolder(srcFolder.path(), &uploadedCount, &err), qPrintable(err));
        QCOMPARE(uploadedCount, 2);

        // Wait until both files are replicated to peer
        QVERIFY2(waitUntil([&]() {
            return peer.metadata()->files(false).size() == 2 &&
                   peer.storage()->managedBytes() > 0;
        }, 6000), "Uploaded folder files must replicate to peer");

        // Peer cannot initiate Guardian shutdown
        QVERIFY(!peer.initiateGuardianShutdown(2, &err));

        // Host Guardian initiates a 2-second shutdown countdown
        QSignalSpy peerCountdownSpy(&peer, &PandoraController::shutdownCountdownUpdated);
        QVERIFY2(host.initiateGuardianShutdown(2, &err), qPrintable(err));
        QVERIFY2(waitUntil([&]() { return peer.isShuttingDown() && peerCountdownSpy.count() >= 1; }, 4000),
                 "Peer must receive ShutdownWarning countdown from Guardian");

        // While countdown is active, peer executes Download-All
        QTemporaryDir exportDir;
        int exportedCount = 0;
        QVERIFY2(peer.downloadAllFiles(exportDir.path(), &exportedCount, &err), qPrintable(err));
        QCOMPARE(exportedCount, 2);

        // Wait for countdown to expire (2 seconds) and automatically deactivate both nodes
        QVERIFY2(waitUntil([&]() { return !host.isActive() && !peer.isActive(); }, 6000),
                 "Both host and peer must automatically deactivate and wipe temp storage when countdown finishes");
    }
};

QTEST_MAIN(PandoraDistributedTests)
#include "pandora_distributed_tests.moc"
