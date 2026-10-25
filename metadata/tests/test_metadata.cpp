// Unit tests for the metadata module. Several MetadataService instances are wired together in-process, which
// exercises exactly the event flow Networking will provide at integration time.

#include <QSignalSpy>
#include <QtTest>
#include <memory>
#include <vector>

#include <pandora/metadata/MetadataService.h>

using namespace pandora::shared;
using pandora::metadata::LogFormat;
using pandora::metadata::MetadataConfig;
using pandora::metadata::MetadataService;

namespace {

Node makeNode(const QString& name, const QString& id) {
    Node n;
    n.id = id;
    n.name = name;
    return n;
}

FileMetadata makeFile(const QString& id, const QString& name, qint64 size, const QStringList& replicas) {
    FileMetadata f;
    f.fileId = id;
    f.name = name;
    f.sizeBytes = size;
    f.replicaNodeIds = replicas;
    return f;
}

/// In-process "network" connecting services. Can be partitioned to test concurrent operations.
class Cluster {
public:
    MetadataService* add(const QString& name, const QString& id, int graceSeconds = 120) {
        MetadataConfig cfg;
        cfg.shutdownGraceSeconds = graceSeconds;
        nodes.push_back(std::make_unique<MetadataService>(makeNode(name, id), cfg));
        MetadataService* s = nodes.back().get();
        s->open();
        QObject::connect(s, &MetadataService::outgoingMessage, [this, s](const Message& m) {
            sent.push_back(m);
            if (linked) deliver(m);
            else held.push_back(m);
        });
        return s;
    }

    void deliver(const Message& m) {
        for (auto& n : nodes)
            if (n->self().id != m.senderId && (m.isBroadcast() || m.recipientId == n->self().id)) n->handleMessage(m);
    }

    void flush() {
        std::vector<Message> pending;
        pending.swap(held);
        for (const Message& m : pending) deliver(m);
    }

    bool join(MetadataService* guardian, MetadataService* joiner) {
        Message reply;
        if (!guardian->admitNavi(joiner->self(), &reply)) return false;
        deliver(reply);
        return true;
    }

    int count(MessageType t) const {
        int c = 0;
        for (const Message& m : sent) c += (m.type == t);
        return c;
    }

    std::vector<std::unique_ptr<MetadataService>> nodes;
    std::vector<Message> sent, held;
    bool linked = true;
};

Pandora makePandora() {
    Pandora p;
    p.id = QStringLiteral("P1");
    p.keyHash = QStringLiteral("hash");
    p.maxNavi = 3;
    return p;
}

}  // namespace

class MetadataTests : public QObject {
    Q_OBJECT
private slots:
    void createAndJoin() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        QVERIFY(a->createPandora(makePandora()));
        QVERIFY(!a->createPandora(makePandora()));  // only once
        QVERIFY(c.join(a, b));
        QVERIFY(b->hasPandora());
        QCOMPARE(b->guardianId(), QString("a"));
        QCOMPARE(b->listNavis().size(), 2);
        QCOMPARE(a->listNavis().size(), 2);
        QVERIFY(!b->isGuardian());
    }

    void capacityIsEnforced() {
        Cluster c;
        auto* a = c.add("A", "a");
        a->createPandora(makePandora());  // maxNavi = 3
        QVERIFY(c.join(a, c.add("B", "b")));
        QVERIFY(c.join(a, c.add("C", "c")));
        auto* d = c.add("D", "d");
        QVERIFY(!c.join(a, d));
    }

    void fileEventsReplicateMetadata() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);

        QVERIFY(b->addMetadata(makeFile("f1", "report.pdf", 1000, {"a", "b"})));
        FileMetadata f;
        QVERIFY(a->getFile("f1", &f));
        QCOMPARE(f.version, qint64(1));
        QCOMPARE(f.replicaNodeIds, (QStringList{"a", "b"}));
        QCOMPARE(a->storageUsage("a"), qint64(1000));
        QCOMPARE(a->storageUsage("b"), qint64(1000));

        // Every Na'vi can edit.
        FileMetadata edit = f;
        edit.sizeBytes = 2500;
        QVERIFY(a->updateMetadata(edit));
        QVERIFY(b->getFile("f1", &f));
        QCOMPARE(f.version, qint64(2));
        QCOMPARE(f.sizeBytes, qint64(2500));
        QCOMPARE(f.modifiedBy, QString("a"));

        // Duplicate names are refused locally.
        QVERIFY(!a->addMetadata(makeFile("f2", "report.pdf", 1, {"a"})));

        // Delete is immediate and final, even if an old upload shows up late.
        QVERIFY(b->removeMetadata("f1"));
        QVERIFY(!a->getFile("f1", nullptr));
        Message late = Message::create(MessageType::FileUpload, "b", {{"file", makeFile("f1", "report.pdf", 1, {}).toJson()}});
        a->handleMessage(late);
        QVERIFY(!a->getFile("f1", nullptr));
    }

    void concurrentDuplicateNamesConverge() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);

        c.linked = false;  // both upload "x.txt" at the same time
        QVERIFY(a->addMetadata(makeFile("fa", "x.txt", 10, {"a"})));
        QVERIFY(b->addMetadata(makeFile("fb", "x.txt", 10, {"b"})));
        c.linked = true;
        c.flush();

        FileMetadata fa, fb;
        QVERIFY(a->getFileByName("x.txt", &fa));
        QVERIFY(b->getFileByName("x.txt", &fb));
        QCOMPARE(fa.fileId, fb.fileId);          // same winner everywhere
        QCOMPARE(a->listFiles().size(), 1);
        QCOMPARE(b->listFiles().size(), 1);
    }

    void concurrentEditsConvergeLastWriteWins() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);
        a->addMetadata(makeFile("f", "doc", 1, {"a"}));

        c.linked = false;
        FileMetadata ea = makeFile("f", "", 111, {});
        FileMetadata eb = makeFile("f", "", 222, {});
        QVERIFY(a->updateMetadata(ea));
        QVERIFY(b->updateMetadata(eb));
        c.linked = true;
        c.flush();

        FileMetadata fa, fb;
        a->getFile("f", &fa);
        b->getFile("f", &fb);
        QCOMPARE(fa.version, qint64(2));
        QCOMPARE(fa.sizeBytes, fb.sizeBytes);
        QCOMPARE(fa.modifiedBy, fb.modifiedBy);
    }

    void leavingNaviTriggersSelfHealingSignal() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);
        a->addMetadata(makeFile("f", "doc", 100, {"a", "b"}));

        QSignalSpy lost(a, &MetadataService::replicaLost);
        QSignalSpy left(a, &MetadataService::naviLeft);
        a->handlePeerLost("b");
        QCOMPARE(left.count(), 1);
        QCOMPARE(lost.count(), 1);
        QCOMPARE(lost.first().at(0).toString(), QString("f"));
        QCOMPARE(a->availableReplicas("f"), QStringList{"a"});
        QCOMPARE(a->listNavis(true).size(), 1);
        QCOMPARE(a->listNavis(false).size(), 2);  // still registered, but disconnected
        a->handlePeerLost("b");                   // idempotent
        QCOMPARE(left.count(), 1);
    }

    void guardianElectionIsDeterministicAndSingle() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        auto* d = c.add("C", "c");
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        c.join(a, d);
        const int before = c.count(MessageType::GuardianChanged);

        // Networking on b and c detects that the Guardian died.
        b->handlePeerLost("a");
        d->handlePeerLost("a");

        QVERIFY(b->guardianId() == d->guardianId());
        QVERIFY(b->guardianId() == "b" || b->guardianId() == "c");
        QCOMPARE(b->pandora().guardianEpoch, qint64(1));
        QCOMPARE(c.count(MessageType::GuardianChanged), before + 1);  // only the winner announces
        QVERIFY(!b->electGuardian());                                   // new Guardian is connected
    }

    void shutdownCountdownWipesEverything() {
        Cluster c;
        auto* a = c.add("A", "a", 1);
        auto* b = c.add("B", "b", 1);
        a->createPandora(makePandora());
        c.join(a, b);
        a->addMetadata(makeFile("f", "doc", 1, {"a"}));

        QVERIFY(!b->shutdownPandora());  // only the Guardian
        QSignalSpy warnB(b, &MetadataService::shutdownWarning);
        QSignalSpy deadA(a, &MetadataService::pandoraDeactivated);
        QSignalSpy deadB(b, &MetadataService::pandoraDeactivated);
        QSignalSpy logsA(a, &MetadataService::finalLogsReady);
        QSignalSpy logsB(b, &MetadataService::finalLogsReady);

        QVERIFY(a->shutdownPandora());
        QCOMPARE(warnB.count(), 1);
        QVERIFY(b->hasPandora());                 // data stays available during the countdown
        QVERIFY(!a->addMetadata(makeFile("g", "late", 1, {"a"})));  // but no new uploads

        QTRY_COMPARE_WITH_TIMEOUT(deadA.count(), 1, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(deadB.count(), 1, 4000);
        QCOMPARE(logsA.count(), 1);               // Guardian receives the final logs
        QVERIFY(logsA.first().at(0).toString().contains("Pandora deactivated"));
        QCOMPARE(logsB.count(), 0);
        QVERIFY(!a->hasPandora());
        QVERIFY(!b->hasPandora());
        QVERIFY(a->listFiles().isEmpty());
        QVERIFY(a->generateLogs().isEmpty());     // logs are gone with the database
    }

    void snapshotRequestSyncsLateNode() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);
        c.linked = false;
        a->addMetadata(makeFile("f", "missed", 5, {"a"}));  // b never saw it
        c.held.clear();
        c.linked = true;
        QVERIFY(!b->getFile("f", nullptr));
        b->syncMetadata();
        QVERIFY(b->getFile("f", nullptr));
    }

    void duplicateMessagesAreIdempotent() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);
        a->addMetadata(makeFile("f", "doc", 5, {"a"}));
        Message upload;
        for (const Message& m : c.sent)
            if (m.type == MessageType::FileUpload) upload = m;
        QSignalSpy added(b, &MetadataService::fileAdded);
        b->handleMessage(upload);
        b->handleMessage(upload);
        QCOMPARE(added.count(), 0);  // already applied once through the live delivery
        QCOMPARE(b->listFiles().size(), 1);
    }

    // ------------------------------------------------------------------ additional coverage

    void onlyGuardianAdmitsAndRequestsLogs() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        auto* d = c.add("C", "c");
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        Message reply;
        QVERIFY(!b->admitNavi(d->self(), &reply));  // not the Guardian
        QVERIFY(!b->requestLogs());
        QVERIFY(a->requestLogs());
    }

    void guardianCollectsClusterLogs() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);
        b->addMetadata(makeFile("f", "x.txt", 1, {"b"}));

        QSignalSpy got(a, &MetadataService::logsReceived);
        QVERIFY(a->requestLogs());
        QCOMPARE(got.count(), 1);
        const QString all = a->generateClusterLogs();
        QVERIFY(all.contains("<A> "));
        QVERIFY(all.contains("<B> "));
        QVERIFY(all.contains("Uploaded 'x.txt'"));
        QVERIFY(!a->generateLogs().contains("<B> "));  // local view stays local
        QVERIFY(a->generateClusterLogs(LogFormat::Json).contains("\"node\""));
        QVERIFY(a->generateLogs(LogFormat::Json).contains("\"category\""));
        QVERIFY(!b->generateClusterLogs().contains("<A> "));  // a non-Guardian never collects logs
    }

    void logsRequestFromNonGuardianIsIgnored() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        auto* d = c.add("C", "c");
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        c.join(a, d);
        const int before = c.count(MessageType::MetadataSync);
        d->handleMessage(Message::create(MessageType::MetadataSync, "b", {{"kind", "logs_request"}}));
        QCOMPARE(c.count(MessageType::MetadataSync), before);  // no reply was produced
    }

    void leavePandoraWipesAndHandsOverGuardian() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        auto* d = c.add("C", "c");
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        c.join(a, d);
        QSignalSpy deadA(a, &MetadataService::pandoraDeactivated);
        QSignalSpy finalA(a, &MetadataService::finalLogsReady);
        QSignalSpy leftB(b, &MetadataService::naviLeft);

        QVERIFY(a->leavePandora());
        QCOMPARE(deadA.count(), 1);
        QCOMPARE(finalA.count(), 0);  // leaving is not a deactivation: no final logs
        QVERIFY(!a->hasPandora());
        QCOMPARE(leftB.count(), 1);
        QVERIFY(b->guardianId() == "b" || b->guardianId() == "c");
        QCOMPARE(b->guardianId(), d->guardianId());
        QVERIFY(!a->leavePandora());  // nothing left to leave

        // The node can start over afterwards with a clean database.
        QVERIFY(a->open());
        QVERIFY(a->createPandora(makePandora()));
        QVERIFY(a->listFiles().isEmpty());
        QCOMPARE(a->listNavis().size(), 1);
    }

    void guardianLeavesDuringCountdown() {
        Cluster c;
        auto* a = c.add("A", "a", 2);
        auto* b = c.add("B", "b", 2);
        auto* d = c.add("C", "c", 2);
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        c.join(a, d);
        QSignalSpy deadB(b, &MetadataService::pandoraDeactivated);
        QSignalSpy deadD(d, &MetadataService::pandoraDeactivated);
        QSignalSpy finalB(b, &MetadataService::finalLogsReady);
        QSignalSpy finalD(d, &MetadataService::finalLogsReady);

        QVERIFY(a->shutdownPandora());
        b->handlePeerLost("a");  // the Guardian disappears during the countdown
        d->handlePeerLost("a");
        const QString newGuardian = b->guardianId();
        QVERIFY(newGuardian == "b" || newGuardian == "c");

        QTRY_COMPARE_WITH_TIMEOUT(deadB.count(), 1, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(deadD.count(), 1, 8000);
        QVERIFY(!b->hasPandora());
        QVERIFY(!d->hasPandora());
        // Exactly the new Guardian receives the final logs, and they show the hand-over.
        QCOMPARE(finalB.count() + finalD.count(), 1);
        const QString logs = (newGuardian == "b" ? finalB : finalD).first().at(0).toString();
        QVERIFY(logs.contains("election"));
        QVERIFY(logs.contains("Pandora deactivated"));
    }

    void fileDatabaseIsErasedOnWipe() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("meta.db");
        MetadataConfig cfg;
        cfg.databasePath = path;
        cfg.shutdownGraceSeconds = 1;
        MetadataService s(makeNode("A", "a"), cfg);
        QVERIFY(s.open());
        QVERIFY(s.createPandora(makePandora()));
        QVERIFY(QFile::exists(path));

        QSignalSpy dead(&s, &MetadataService::pandoraDeactivated);
        QVERIFY(s.shutdownPandora());
        QTRY_COMPARE_WITH_TIMEOUT(dead.count(), 1, 4000);
        for (const QString& suffix : {QString(), QString("-wal"), QString("-shm"), QString("-journal")})
            QVERIFY2(!QFile::exists(path + suffix), qPrintable(path + suffix));

        // Leaving also erases the file.
        QVERIFY(s.open());
        QVERIFY(s.createPandora(makePandora()));
        QVERIFY(QFile::exists(path));
        QVERIFY(s.leavePandora());
        QVERIFY(!QFile::exists(path));
    }

    void replicaUpdatesPropagate() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        auto* d = c.add("C", "c");
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        c.join(a, d);
        a->addMetadata(makeFile("f", "doc", 7, {"a"}));
        QSignalSpy changed(b, &MetadataService::replicasChanged);

        QVERIFY(a->updateReplicas("f", {"b", "c"}, {}));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(a->availableReplicas("f"), (QStringList{"a", "b", "c"}));
        QCOMPARE(b->availableReplicas("f"), (QStringList{"a", "b", "c"}));
        QCOMPARE(d->storageUsage("c"), qint64(7));

        QVERIFY(a->updateReplicas("f", {}, {"a"}));
        QCOMPARE(d->availableReplicas("f"), (QStringList{"b", "c"}));
        QCOMPARE(d->storageUsage("a"), qint64(0));

        QVERIFY(!a->updateReplicas("nope", {"b"}, {}));  // unknown file
        QVERIFY(a->updateReplicas("f", {"ghost"}, {}));    // unknown node is not recorded
        QCOMPARE(a->availableReplicas("f"), (QStringList{"b", "c"}));
    }

    void forgedControlMessagesAreIgnored() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        auto* d = c.add("C", "c");
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        c.join(a, d);

        QSignalSpy warn(d, &MetadataService::shutdownWarning);
        d->handleMessage(Message::create(MessageType::ShutdownWarning, "b", {{"secondsRemaining", 1}}));
        d->handleMessage(Message::create(MessageType::PandoraDeactivate, "b", {}));
        QCOMPARE(warn.count(), 0);
        QVERIFY(d->hasPandora());
        QVERIFY(d->pandora().state == PandoraState::Active);

        d->handleMessage(Message::create(MessageType::GuardianChanged, "b",
                                         {{"oldGuardianId", "a"}, {"newGuardianId", "c"}, {"epoch", 1}}));  // sender != successor
        d->handleMessage(Message::create(MessageType::GuardianChanged, "c",
                                         {{"oldGuardianId", "a"}, {"newGuardianId", "c"}, {"epoch", 0}}));  // not newer
        QCOMPARE(d->guardianId(), QString("a"));

        // Not addressed to this node, our own echo, and malformed input are all dropped.
        d->handleMessage(Message::create(MessageType::FileUpload, "a", {{"file", makeFile("z", "z", 1, {}).toJson()}}, "b"));
        d->handleMessage(Message::create(MessageType::FileUpload, "c", {{"file", makeFile("y", "y", 1, {}).toJson()}}));
        d->handleMessage(Message());
        QVERIFY(d->listFiles().isEmpty());
    }

    void localApiValidatesInput() {
        MetadataService lone(makeNode("A", "a"));
        QVERIFY(!lone.createPandora(makePandora()));  // not opened yet
        QVERIFY(lone.open());
        QVERIFY(!lone.addMetadata(makeFile("f", "x", 1, {})));  // no Pandora yet
        Pandora noKey;
        noKey.id = "P";
        QVERIFY(!lone.createPandora(noKey));
        Pandora dflt = makePandora();
        dflt.maxNavi = 0;
        QVERIFY(lone.createPandora(dflt));
        QCOMPARE(lone.pandora().maxNavi, 20);  // default capacity, configurable per Pandora
        QVERIFY(lone.isGuardian());

        QVERIFY(!lone.addMetadata(makeFile("", "x", 1, {})));
        QVERIFY(!lone.addMetadata(makeFile("f", "", 1, {})));
        QVERIFY(lone.addMetadata(makeFile("f", "x", 1, {"a", "ghost"})));
        FileMetadata f;
        QVERIFY(lone.getFile("f", &f));
        QCOMPARE(f.replicaNodeIds, QStringList{"a"});  // unknown node dropped
        QVERIFY(!lone.addMetadata(makeFile("f", "other", 1, {})));  // id already used
        QVERIFY(lone.addMetadata(makeFile("g", "y", 1, {})));

        FileMetadata rename = f;
        rename.name = "y";
        QVERIFY(!lone.updateMetadata(rename));  // would duplicate a name
        QVERIFY(!lone.updateMetadata(makeFile("nope", "n", 1, {})));
        QVERIFY(!lone.removeMetadata("nope"));

        for (int i = 2; i <= 4; ++i) {
            QVERIFY(lone.updateMetadata(f));
            QVERIFY(lone.getFile("f", &f));
            QCOMPARE(f.version, qint64(i));
        }
        QVERIFY(lone.removeMetadata("f"));
        QVERIFY(!lone.addMetadata(makeFile("f", "x", 1, {})));  // deleted ids are never reused
    }

    void snapshotsNeverResurrectOrOverwriteLiveState() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        auto* d = c.add("C", "c");
        a->createPandora([] { auto p = makePandora(); p.maxNavi = 5; return p; }());
        c.join(a, b);
        c.join(a, d);
        a->addMetadata(makeFile("f", "doc", 1, {"a", "b"}));
        const QJsonObject stale = a->exportSnapshot();

        QVERIFY(a->updateReplicas("f", {}, {"b"}));       // live event removes b's replica
        QVERIFY(d->importSnapshot(stale));
        QCOMPARE(d->availableReplicas("f"), QStringList{"a"});  // stale snapshot did not bring it back

        d->handlePeerLost("b");
        QVERIFY(d->importSnapshot(stale));                 // snapshot still lists b as connected
        QCOMPARE(d->listNavis(true).size(), 2);            // ... but a departed Na'vi stays departed

        QJsonObject foreign = stale;
        QJsonObject pj = foreign["pandora"].toObject();
        pj["id"] = "OTHER";
        foreign["pandora"] = pj;
        QVERIFY(!d->importSnapshot(foreign));
        QVERIFY(!d->importSnapshot(QJsonObject()));
    }

    void snapshotCarriesDeletions() {
        Cluster c;
        auto* a = c.add("A", "a");
        auto* b = c.add("B", "b");
        a->createPandora(makePandora());
        c.join(a, b);
        a->addMetadata(makeFile("f", "doc", 1, {"a"}));
        c.linked = false;
        QVERIFY(a->removeMetadata("f"));  // b misses the FILE_DELETE
        c.held.clear();
        c.linked = true;
        QVERIFY(b->getFile("f", nullptr));
        QVERIFY(b->importSnapshot(a->exportSnapshot()));
        QVERIFY(!b->getFile("f", nullptr));
    }

    void noJoinOrUploadDuringShutdown() {
        Cluster c;
        auto* a = c.add("A", "a", 30);
        a->createPandora(makePandora());
        QVERIFY(a->shutdownPandora());
        QVERIFY(!a->shutdownPandora());  // already counting down
        Message reply;
        QVERIFY(!a->admitNavi(makeNode("B", "b"), &reply));
        QVERIFY(!a->addMetadata(makeFile("f", "x", 1, {})));
    }

    void departureOfUnknownOrSelfIsHarmless() {
        Cluster c;
        auto* a = c.add("A", "a");
        a->createPandora(makePandora());
        QSignalSpy left(a, &MetadataService::naviLeft);
        a->handlePeerLost("nobody");
        a->handlePeerLost("a");  // never remove ourselves
        QCOMPARE(left.count(), 0);
        QCOMPARE(a->listNavis().size(), 1);
        QVERIFY(!a->electGuardian());  // Guardian is alive
    }
};

QTEST_GUILESS_MAIN(MetadataTests)
#include "test_metadata.moc"
