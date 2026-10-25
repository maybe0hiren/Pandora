#include <pandora/metadata/MetadataService.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QTimer>
#include <algorithm>
#include <mutex>

#include "MetadataStore.h"

using namespace pandora::shared;

namespace pandora::metadata {

namespace {
using Lock = std::lock_guard<std::recursive_mutex>;

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QStringList toStringList(const QJsonValue& v) {
    QStringList out;
    for (const QJsonValue& e : v.toArray()) out << e.toString();
    return out;
}
}  // namespace

struct MetadataService::Impl {
    Node self;
    MetadataConfig config;
    std::unique_ptr<MetadataStore> store;
    mutable std::recursive_mutex mutex;
    QTimer countdown;
    int remaining = 0;
    QMap<QString, QList<LogEntry>> collectedLogs;  ///< Guardian only, memory only, wiped with everything else
};

MetadataService::MetadataService(const Node& self, MetadataConfig config, QObject* parent)
    : QObject(parent), d(new Impl) {
    qRegisterMetaType<pandora::shared::Message>("pandora::shared::Message");
    qRegisterMetaType<pandora::shared::Node>("pandora::shared::Node");
    qRegisterMetaType<pandora::shared::FileMetadata>("pandora::shared::FileMetadata");
    d->self = self;
    d->self.connected = true;
    d->config = std::move(config);
    d->countdown.setInterval(1000);
    connect(&d->countdown, &QTimer::timeout, this, &MetadataService::onCountdownTick);
}

MetadataService::~MetadataService() = default;

// ======================================================================== helpers

void MetadataService::log(const QString& level, const QString& category, const QString& message) {
    if (d->store) d->store->appendLog(level, category, message);
}

void MetadataService::send(MessageType type, const QJsonObject& payload, const QString& recipientId) {
    emit outgoingMessage(Message::create(type, d->self.id, payload, recipientId));
}

Result MetadataService::requireActive(Pandora* out) const {
    if (!d->store || !d->store->isOpen()) return Result::failure(QStringLiteral("metadata store is not open"));
    Pandora p;
    if (!d->store->loadPandora(&p)) return Result::failure(QStringLiteral("no Pandora created or joined yet"));
    if (p.state != PandoraState::Active) return Result::failure(QStringLiteral("Pandora is shutting down"));
    if (out) *out = p;
    return Result::success();
}

QStringList MetadataService::onlyConnected(const QStringList& naviIds) const {
    QStringList out;
    for (const QString& id : naviIds) {
        Node n;
        if (d->store->getNavi(id, &n) && n.connected && !out.contains(id)) out << id;
    }
    return out;
}

// ======================================================================== lifecycle

Result MetadataService::open() {
    Lock lock(d->mutex);
    if (d->store && d->store->isOpen()) return Result::success();
    d->store = std::make_unique<MetadataStore>();
    QString err;
    if (!d->store->open(d->config.databasePath, &err)) {
        d->store.reset();
        return Result::failure(QStringLiteral("cannot open metadata database: ") + err);
    }
    return Result::success();
}

Result MetadataService::createPandora(Pandora pandora) {
    Lock lock(d->mutex);
    if (!d->store) return Result::failure(QStringLiteral("call open() first"));
    if (d->store->loadPandora(nullptr)) return Result::failure(QStringLiteral("a Pandora already exists on this node"));
    if (pandora.id.isEmpty() || pandora.keyHash.isEmpty()) return Result::failure(QStringLiteral("Pandora id and keyHash are required"));
    if (!d->self.isValid()) return Result::failure(QStringLiteral("local node needs an id and a name"));

    pandora.guardianId = d->self.id;
    pandora.state = PandoraState::Active;
    pandora.guardianEpoch = 0;
    if (pandora.maxNavi <= 0) pandora.maxNavi = constants::kDefaultMaxNavi;
    if (pandora.createdAtMs == 0) pandora.createdAtMs = nowMs();

    Node me = d->self;
    me.connected = true;
    me.joinedAtMs = nowMs();
    if (!d->store->savePandora(pandora) || !d->store->upsertNavi(me))
        return Result::failure(d->store->lastError());

    log(QStringLiteral("INFO"), QStringLiteral("lifecycle"),
        QStringLiteral("Pandora %1 created, Guardian %2 (%3)").arg(pandora.id, me.name, me.id));
    send(MessageType::CreatePandora, {{key::kPandora, pandora.toJson()}, {key::kNode, me.toJson()}});
    return Result::success();
}

Result MetadataService::admitNavi(Node candidate, Message* reply) {
    Lock lock(d->mutex);
    Pandora p;
    Result r = requireActive(&p);
    if (!r) return r;
    if (p.guardianId != d->self.id)
        return Result::failure(QStringLiteral("only the Guardian admits new Na'vi; forward the JOIN_REQUEST to %1").arg(p.guardianId));
    if (!candidate.isValid()) return Result::failure(QStringLiteral("invalid node (id and name required)"));

    Node existing;
    if (d->store->getNavi(candidate.id, &existing))
        return Result::failure(QStringLiteral("node id already used (identities are single-use)"));
    if (d->store->connectedCount() >= p.maxNavi) {
        log(QStringLiteral("WARN"), QStringLiteral("membership"),
            QStringLiteral("Join of %1 rejected: Pandora is full (%2)").arg(candidate.name).arg(p.maxNavi));
        return Result::failure(QStringLiteral("Pandora is full"));
    }

    candidate.connected = true;
    candidate.joinedAtMs = nowMs();
    if (!d->store->upsertNavi(candidate)) return Result::failure(d->store->lastError());
    log(QStringLiteral("INFO"), QStringLiteral("membership"),
        QStringLiteral("%1 (%2) joined").arg(candidate.name, candidate.id));

    emit naviJoined(candidate);
    send(MessageType::NaviJoined, {{key::kNode, candidate.toJson()}});
    if (reply)
        *reply = Message::create(MessageType::JoinAccepted, d->self.id, {{key::kSnapshot, exportSnapshot()}}, candidate.id);
    return Result::success();
}

Result MetadataService::leavePandora() {
    Lock lock(d->mutex);
    if (!d->store || !d->store->loadPandora(nullptr)) return Result::failure(QStringLiteral("not part of a Pandora"));
    send(MessageType::NaviLeft, {{key::kNaviId, d->self.id}});
    wipe(false);
    return Result::success();
}

// ======================================================================== file metadata (local origin)

Result MetadataService::addMetadata(FileMetadata file) {
    Lock lock(d->mutex);
    Result r = requireActive();
    if (!r) return r;
    if (!file.isValid()) return Result::failure(QStringLiteral("file id and name are required"));
    if (d->store->isTombstoned(file.fileId) || d->store->getFile(file.fileId, nullptr))
        return Result::failure(QStringLiteral("file id already known; use updateMetadata()"));
    if (d->store->getFileByName(file.name, nullptr))
        return Result::failure(QStringLiteral("a file named '%1' already exists").arg(file.name));

    if (file.version <= 0) file.version = 1;
    if (file.modifiedAtMs == 0) file.modifiedAtMs = nowMs();
    if (file.modifiedBy.isEmpty()) file.modifiedBy = d->self.id;
    file.replicaNodeIds = onlyConnected(file.replicaNodeIds);
    if (!d->store->upsertFile(file)) return Result::failure(d->store->lastError());

    FileMetadata stored;
    d->store->getFile(file.fileId, &stored);
    log(QStringLiteral("INFO"), QStringLiteral("file"),
        QStringLiteral("Uploaded '%1' v%2 (%3 bytes) replicas=[%4]")
            .arg(stored.name).arg(stored.version).arg(stored.sizeBytes).arg(stored.replicaNodeIds.join(QLatin1Char(','))));
    emit fileAdded(stored);
    send(MessageType::FileUpload, {{key::kFile, stored.toJson()}});
    return Result::success();
}

Result MetadataService::updateMetadata(const FileMetadata& file) {
    Lock lock(d->mutex);
    Result r = requireActive();
    if (!r) return r;
    FileMetadata current;
    if (!d->store->getFile(file.fileId, &current)) return Result::failure(QStringLiteral("unknown file"));

    FileMetadata next = current;
    next.sizeBytes = file.sizeBytes;
    next.checksum = file.checksum;
    if (!file.replicaNodeIds.isEmpty()) next.replicaNodeIds = onlyConnected(file.replicaNodeIds);
    if (!file.name.isEmpty() && file.name != current.name) {
        if (d->store->getFileByName(file.name, nullptr))
            return Result::failure(QStringLiteral("a file named '%1' already exists").arg(file.name));
        next.name = file.name;
    }
    next.version = current.version + 1;
    next.modifiedAtMs = std::max(nowMs(), current.modifiedAtMs);
    next.modifiedBy = d->self.id;
    if (!d->store->upsertFile(next)) return Result::failure(d->store->lastError());

    d->store->getFile(next.fileId, &next);
    log(QStringLiteral("INFO"), QStringLiteral("file"),
        QStringLiteral("Edited '%1' -> v%2 by %3").arg(next.name).arg(next.version).arg(next.modifiedBy));
    emit fileUpdated(next);
    send(MessageType::FileUpdate, {{key::kFile, next.toJson()}});
    return Result::success();
}

Result MetadataService::removeMetadata(const QString& fileId) {
    Lock lock(d->mutex);
    Result r = requireActive();
    if (!r) return r;
    FileMetadata f;
    if (!d->store->getFile(fileId, &f)) return Result::failure(QStringLiteral("unknown file"));
    d->store->deleteFile(fileId);
    d->store->addTombstone(fileId, f.name, nowMs());
    log(QStringLiteral("INFO"), QStringLiteral("file"), QStringLiteral("Deleted '%1'").arg(f.name));
    emit fileRemoved(fileId);
    send(MessageType::FileDelete, {{key::kFileId, fileId}});
    return Result::success();
}

Result MetadataService::updateReplicas(const QString& fileId, const QStringList& add, const QStringList& remove) {
    Lock lock(d->mutex);
    Result r = requireActive();
    if (!r) return r;
    if (!d->store->getFile(fileId, nullptr)) return Result::failure(QStringLiteral("unknown file"));
    applyReplicaUpdate(fileId, add, remove);
    send(MessageType::MetadataSync, {{key::kKind, QString(sync::kReplicaUpdate)},
                                     {key::kFileId, fileId},
                                     {key::kAddReplicas, QJsonArray::fromStringList(add)},
                                     {key::kRemoveReplicas, QJsonArray::fromStringList(remove)}});
    return Result::success();
}

void MetadataService::applyReplicaUpdate(const QString& fileId, const QStringList& add, const QStringList& remove) {
    if (!d->store->getFile(fileId, nullptr)) return;
    for (const QString& id : onlyConnected(add)) d->store->addReplica(fileId, id);
    for (const QString& id : remove) d->store->removeReplica(fileId, id);
    log(QStringLiteral("INFO"), QStringLiteral("sync"),
        QStringLiteral("Replicas of %1: +[%2] -[%3]").arg(fileId, add.join(QLatin1Char(',')), remove.join(QLatin1Char(','))));
    emit replicasChanged(fileId);
}

// ======================================================================== remote file events

MetadataService::Applied MetadataService::applyRemoteFile(const FileMetadata& incoming) {
    if (!incoming.isValid() || d->store->isTombstoned(incoming.fileId)) return Applied::Ignored;

    FileMetadata file = incoming;
    file.replicaNodeIds = onlyConnected(file.replicaNodeIds);

    FileMetadata current;
    if (d->store->getFile(file.fileId, &current)) {
        const int order = file.compareWrite(current);
        if (order > 0) {
            // A rename onto a name owned by a different file would violate UNIQUE(name): resolve like an upload conflict.
            FileMetadata clash;
            if (file.name != current.name && d->store->getFileByName(file.name, &clash) && clash.fileId != file.fileId) {
                if (!file.supersedes(clash)) return Applied::Ignored;
                d->store->deleteFile(clash.fileId);
                d->store->addTombstone(clash.fileId, clash.name, nowMs());
                emit fileRemoved(clash.fileId);
            }
            if (!d->store->upsertFile(file)) return Applied::Ignored;
            FileMetadata stored;
            d->store->getFile(file.fileId, &stored);
            log(QStringLiteral("INFO"), QStringLiteral("file"),
                QStringLiteral("Remote edit of '%1' -> v%2 by %3").arg(stored.name).arg(stored.version).arg(stored.modifiedBy));
            emit fileUpdated(stored);
            return Applied::Updated;
        }
        // order == 0: the same write seen again (e.g. in a snapshot). Replica locations are deliberately NOT merged here:
        // live replica_update events are authoritative, and a stale snapshot must never claim a replica that no longer
        // exists (a false "replica exists" risks data loss; a missing one only causes an extra copy).
        return Applied::Ignored;  // older write loses (Last Write Wins)
    }

    FileMetadata clash;
    if (d->store->getFileByName(file.name, &clash)) {  // different id, same name: duplicate names are not allowed
        if (!file.supersedes(clash)) {
            d->store->addTombstone(file.fileId, file.name, nowMs());
            log(QStringLiteral("WARN"), QStringLiteral("file"),
                QStringLiteral("Duplicate name '%1': kept %2, dropped %3").arg(file.name, clash.fileId, file.fileId));
            return Applied::Ignored;
        }
        d->store->deleteFile(clash.fileId);
        d->store->addTombstone(clash.fileId, clash.name, nowMs());
        log(QStringLiteral("WARN"), QStringLiteral("file"),
            QStringLiteral("Duplicate name '%1': kept %2, dropped %3").arg(file.name, file.fileId, clash.fileId));
        emit fileRemoved(clash.fileId);
    }
    if (!d->store->upsertFile(file)) return Applied::Ignored;
    FileMetadata stored;
    d->store->getFile(file.fileId, &stored);
    log(QStringLiteral("INFO"), QStringLiteral("file"),
        QStringLiteral("Remote upload '%1' v%2 by %3").arg(stored.name).arg(stored.version).arg(stored.modifiedBy));
    emit fileAdded(stored);
    return Applied::Inserted;
}

void MetadataService::applyRemoteDelete(const QString& fileId) {
    FileMetadata f;
    const bool known = d->store->getFile(fileId, &f);
    d->store->addTombstone(fileId, known ? f.name : QString(), nowMs());  // also blocks late uploads/edits
    if (!known) return;
    d->store->deleteFile(fileId);
    log(QStringLiteral("INFO"), QStringLiteral("file"), QStringLiteral("Remote delete of '%1'").arg(f.name));
    emit fileRemoved(fileId);
}

// ======================================================================== membership & election

void MetadataService::applyNaviLeft(const QString& naviId) {
    if (naviId == d->self.id) return;
    Node n;
    if (!d->store->getNavi(naviId, &n) || !n.connected) return;  // unknown or already handled

    d->store->setNaviConnected(naviId, false);
    const QStringList affected = d->store->removeReplicasOfNavi(naviId);
    log(QStringLiteral("INFO"), QStringLiteral("membership"), QStringLiteral("%1 (%2) left").arg(n.name, naviId));
    emit naviLeft(naviId);
    for (const QString& fileId : affected) emit replicaLost(fileId, naviId);

    Pandora p;
    if (d->store->loadPandora(&p) && p.guardianId == naviId) electAfterGuardianLoss(nullptr);
}

Result MetadataService::electAfterGuardianLoss(QString* newGuardianId) {
    Pandora p;
    if (!d->store->loadPandora(&p)) return Result::failure(QStringLiteral("no Pandora"));

    QStringList candidates;
    for (const Node& n : d->store->navis(true))
        if (n.id != p.guardianId) candidates << n.id;
    std::sort(candidates.begin(), candidates.end());
    if (candidates.isEmpty()) {
        log(QStringLiteral("ERROR"), QStringLiteral("election"), QStringLiteral("No Na'vi left to become Guardian"));
        return Result::failure(QStringLiteral("no connected Na'vi left"));
    }

    // Pseudo-random but identical on every node: hash of public, replicated values only.
    const qint64 epoch = p.guardianEpoch + 1;
    const QByteArray seed = QStringLiteral("%1:%2:%3").arg(p.id).arg(epoch).arg(p.guardianId).toUtf8();
    const QByteArray digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<quint8>(digest.at(i));
    const QString winner = candidates.at(static_cast<int>(v % static_cast<quint64>(candidates.size())));

    const QString old = p.guardianId;
    p.guardianId = winner;
    p.guardianEpoch = epoch;
    if (!d->store->savePandora(p)) return Result::failure(d->store->lastError());
    log(QStringLiteral("INFO"), QStringLiteral("election"),
        QStringLiteral("Guardian %1 -> %2 (epoch %3)").arg(old, winner).arg(epoch));
    emit guardianChanged(old, winner);
    if (winner == d->self.id)  // exactly one node announces; the others already computed the same result
        send(MessageType::GuardianChanged, {{key::kOldGuardianId, old}, {key::kNewGuardianId, winner},
                                            {key::kEpoch, static_cast<double>(epoch)}});
    if (p.state == PandoraState::ShuttingDown && winner == d->self.id) requestLogsInternal();
    if (newGuardianId) *newGuardianId = winner;
    return Result::success();
}

Result MetadataService::electGuardian(QString* newGuardianId) {
    Lock lock(d->mutex);
    Pandora p;
    if (!d->store || !d->store->loadPandora(&p)) return Result::failure(QStringLiteral("no Pandora"));
    Node g;
    if (d->store->getNavi(p.guardianId, &g) && g.connected)
        return Result::failure(QStringLiteral("the Guardian is still connected"));
    return electAfterGuardianLoss(newGuardianId);
}

void MetadataService::applyGuardianChanged(const Message& m) {
    Pandora p;
    if (!d->store->loadPandora(&p)) return;
    const QString newG = m.payload.value(key::kNewGuardianId).toString();
    const qint64 epoch = static_cast<qint64>(m.payload.value(key::kEpoch).toDouble());
    if (newG.isEmpty() || m.senderId != newG) {
        log(QStringLiteral("WARN"), QStringLiteral("protocol"), QStringLiteral("Ignored GUARDIAN_CHANGED from non-successor"));
        return;
    }
    // Higher epoch wins; on equal epochs the lowest id wins so that all nodes converge on one Guardian.
    const bool newer = epoch > p.guardianEpoch || (epoch == p.guardianEpoch && newG < p.guardianId);
    if (!newer || newG == p.guardianId) return;
    if (!d->store->getNavi(newG, nullptr)) {
        log(QStringLiteral("WARN"), QStringLiteral("protocol"), QStringLiteral("GUARDIAN_CHANGED names unknown node"));
        return;
    }
    const QString old = p.guardianId;
    p.guardianId = newG;
    p.guardianEpoch = epoch;
    d->store->savePandora(p);
    log(QStringLiteral("INFO"), QStringLiteral("election"), QStringLiteral("Guardian %1 -> %2 (announced, epoch %3)").arg(old, newG).arg(epoch));
    emit guardianChanged(old, newG);
    if (p.state == PandoraState::ShuttingDown && newG == d->self.id) requestLogsInternal();
}

// ======================================================================== shutdown

Result MetadataService::shutdownPandora() {
    Lock lock(d->mutex);
    Pandora p;
    Result r = requireActive(&p);
    if (!r) return r;
    if (p.guardianId != d->self.id) return Result::failure(QStringLiteral("only the Guardian can deactivate Pandora"));
    const int seconds = d->config.shutdownGraceSeconds;
    send(MessageType::ShutdownWarning, {{key::kSecondsRemaining, seconds}});
    beginCountdown(seconds);
    return Result::success();
}

void MetadataService::beginCountdown(int seconds) {
    Pandora p;
    if (d->store->loadPandora(&p)) {
        p.state = PandoraState::ShuttingDown;
        d->store->savePandora(p);
    }
    d->remaining = seconds;
    log(QStringLiteral("WARN"), QStringLiteral("shutdown"), QStringLiteral("Pandora will deactivate in %1 s").arg(seconds));
    d->countdown.start();
    emit shutdownWarning(seconds);
    requestLogsInternal();  // no-op unless this node is the Guardian
}

void MetadataService::onCountdownTick() {
    Lock lock(d->mutex);
    if (!d->store) {
        d->countdown.stop();
        return;
    }
    --d->remaining;
    emit shutdownCountdown(d->remaining);
    if (d->remaining > 0) return;
    Pandora p;
    if (d->store->loadPandora(&p) && p.guardianId == d->self.id) send(MessageType::PandoraDeactivate, {});
    wipe(true);
}

void MetadataService::wipe(bool produceFinalLogs) {
    if (!d->store) return;
    Pandora p;
    const bool wasGuardian = d->store->loadPandora(&p) && p.guardianId == d->self.id;
    QString finalLogs;
    if (wasGuardian && produceFinalLogs) {
        log(QStringLiteral("INFO"), QStringLiteral("shutdown"), QStringLiteral("Pandora deactivated"));
        finalLogs = renderLogs(LogFormat::Text, true);
    }
    d->countdown.stop();
    d->remaining = 0;
    d->collectedLogs.clear();
    d->store->closeAndErase();
    d->store.reset();
    if (wasGuardian && produceFinalLogs) emit finalLogsReady(finalLogs);
    emit pandoraDeactivated();
}

// ======================================================================== logs

QString MetadataService::renderLogs(LogFormat format, bool cluster) const {
    struct Line {
        QString node;
        LogEntry entry;
    };
    QList<Line> lines;
    if (d->store)
        for (const LogEntry& e : d->store->logs()) lines.append({d->self.name, e});
    if (cluster && d->store) {
        for (auto it = d->collectedLogs.cbegin(); it != d->collectedLogs.cend(); ++it) {
            Node n;
            const QString label = d->store->getNavi(it.key(), &n) ? n.name : it.key();
            for (const LogEntry& e : it.value()) lines.append({label, e});
        }
        std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) {
            if (a.entry.timestampMs != b.entry.timestampMs) return a.entry.timestampMs < b.entry.timestampMs;
            if (a.node != b.node) return a.node < b.node;
            return a.entry.seq < b.entry.seq;
        });
    }
    if (format == LogFormat::Json) {
        QJsonArray arr;
        for (const Line& l : lines) {
            QJsonObject o{{"seq", static_cast<double>(l.entry.seq)},
                          {"timestampMs", static_cast<double>(l.entry.timestampMs)},
                          {"level", l.entry.level},
                          {"category", l.entry.category},
                          {"message", l.entry.message}};
            if (cluster) o.insert(QStringLiteral("node"), l.node);
            arr.append(o);
        }
        return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
    QStringList out;
    for (const Line& l : lines)
        out << QStringLiteral("%1 %2[%3] %4: %5")
                   .arg(QDateTime::fromMSecsSinceEpoch(l.entry.timestampMs).toString(Qt::ISODateWithMs),
                        cluster ? QLatin1Char('<') + l.node + QStringLiteral("> ") : QString(), l.entry.level,
                        l.entry.category, l.entry.message);
    return out.join(QLatin1Char('\n'));
}

QString MetadataService::generateLogs(LogFormat format) const {
    Lock lock(d->mutex);
    return renderLogs(format, false);
}

QString MetadataService::generateClusterLogs(LogFormat format) const {
    Lock lock(d->mutex);
    return renderLogs(format, true);
}

void MetadataService::requestLogsInternal() {
    Pandora p;
    if (!d->store || !d->store->loadPandora(&p) || p.guardianId != d->self.id) return;
    d->collectedLogs.clear();  // a new request replaces what was collected before
    send(MessageType::MetadataSync, {{key::kKind, QString(sync::kLogsRequest)}});
}

Result MetadataService::requestLogs() {
    Lock lock(d->mutex);
    Pandora p;
    if (!d->store || !d->store->loadPandora(&p)) return Result::failure(QStringLiteral("no Pandora"));
    if (p.guardianId != d->self.id) return Result::failure(QStringLiteral("only the Guardian can request logs"));
    requestLogsInternal();
    return Result::success();
}

QList<LogEntry> MetadataService::logEntries() const {
    Lock lock(d->mutex);
    return d->store ? d->store->logs() : QList<LogEntry>();
}

// ======================================================================== synchronisation

void MetadataService::syncMetadata() {
    Lock lock(d->mutex);
    if (!d->store || !d->store->loadPandora(nullptr)) return;
    send(MessageType::MetadataSync, {{key::kKind, QString(sync::kSnapshotRequest)}});
}

QJsonObject MetadataService::exportSnapshot() const {
    Lock lock(d->mutex);
    QJsonObject snap;
    if (!d->store) return snap;
    Pandora p;
    if (d->store->loadPandora(&p)) snap.insert(key::kPandora, p.toJson());
    QJsonArray navis;
    for (const Node& n : d->store->navis(false)) navis.append(n.toJson());
    snap.insert(QStringLiteral("navis"), navis);
    QJsonArray files;
    for (const FileMetadata& f : d->store->files()) files.append(f.toJson());
    snap.insert(QStringLiteral("files"), files);
    QJsonArray tombs;
    for (const auto& t : d->store->tombstones())
        tombs.append(QJsonObject{{"fileId", t.first}, {"name", t.second}});
    snap.insert(QStringLiteral("tombstones"), tombs);
    return snap;
}

Result MetadataService::importSnapshot(const QJsonObject& snap) {
    Lock lock(d->mutex);
    if (!d->store) return Result::failure(QStringLiteral("call open() first"));

    const Pandora remote = Pandora::fromJson(snap.value(key::kPandora).toObject());
    if (!remote.isValid()) return Result::failure(QStringLiteral("snapshot has no valid Pandora"));
    Pandora local;
    if (d->store->loadPandora(&local)) {
        if (local.id != remote.id) return Result::failure(QStringLiteral("snapshot belongs to a different Pandora"));
        if (remote.guardianEpoch > local.guardianEpoch) {
            const QString old = local.guardianId;
            local.guardianId = remote.guardianId;
            local.guardianEpoch = remote.guardianEpoch;
            d->store->savePandora(local);
            if (old != local.guardianId) emit guardianChanged(old, local.guardianId);
        }
    } else if (!d->store->savePandora(remote)) {
        return Result::failure(d->store->lastError());
    }

    // Na'vi: "disconnected" is terminal, so a snapshot can only add nodes or report departures, never resurrect.
    for (const QJsonValue& v : snap.value(QStringLiteral("navis")).toArray()) {
        const Node n = Node::fromJson(v.toObject());
        if (!n.isValid()) continue;
        Node known;
        if (!d->store->getNavi(n.id, &known)) {
            d->store->upsertNavi(n);
            if (n.connected && n.id != d->self.id) emit naviJoined(n);
        } else if (!n.connected && known.connected) {
            applyNaviLeft(n.id);
        }
    }
    Node me;
    if (!d->store->getNavi(d->self.id, &me)) {  // a joining node must see itself in its own registry
        Node self = d->self;
        self.connected = true;
        self.joinedAtMs = self.joinedAtMs ? self.joinedAtMs : nowMs();
        d->store->upsertNavi(self);
    }

    for (const QJsonValue& v : snap.value(QStringLiteral("tombstones")).toArray()) {
        const QJsonObject t = v.toObject();
        applyRemoteDelete(t.value(QStringLiteral("fileId")).toString());
    }
    for (const QJsonValue& v : snap.value(QStringLiteral("files")).toArray())
        applyRemoteFile(FileMetadata::fromJson(v.toObject()));

    log(QStringLiteral("INFO"), QStringLiteral("sync"), QStringLiteral("Snapshot merged"));
    emit metadataSynced();
    return Result::success();
}

// ======================================================================== event dispatch

void MetadataService::handlePeerLost(const QString& naviId) {
    Lock lock(d->mutex);
    if (d->store && d->store->loadPandora(nullptr)) applyNaviLeft(naviId);
}

void MetadataService::handleMessage(const Message& m) {
    Lock lock(d->mutex);
    if (!d->store || !d->store->isOpen()) return;
    if (!m.isValid()) {
        log(QStringLiteral("WARN"), QStringLiteral("protocol"), QStringLiteral("Dropped malformed message"));
        return;
    }
    if (m.senderId == d->self.id) return;                          // never process our own echo
    if (!m.isBroadcast() && m.recipientId != d->self.id) return;   // not addressed to us
    if (m.type == MessageType::FileRequest || m.type == MessageType::FileResponse) return;  // Storage's business

    Pandora p;
    const bool joined = d->store->loadPandora(&p);
    if (!joined && m.type != MessageType::JoinAccepted) return;
    if (d->store->wasApplied(m.messageId)) return;                 // duplicate delivery
    d->store->markApplied(m.messageId);

    switch (m.type) {
    case MessageType::CreatePandora:
        // Announcement only (used for LAN discovery). A node becomes a member solely through JOIN_ACCEPTED, otherwise
        // a bystander would adopt the Pandora and then ignore its real join snapshot.
        break;
    case MessageType::JoinRequest: {
        const Node candidate = Node::fromJson(m.payload.value(key::kNode).toObject());
        if (candidate.isValid()) emit joinRequested(candidate, m.senderId);
        break;
    }
    case MessageType::JoinAccepted: {
        if (joined) break;  // already in a Pandora
        const Result r = importSnapshot(m.payload.value(key::kSnapshot).toObject());
        if (r) log(QStringLiteral("INFO"), QStringLiteral("lifecycle"), QStringLiteral("Joined Pandora via %1").arg(m.senderId));
        else emit errorOccurred(r.error);
        break;
    }
    case MessageType::NaviJoined: {
        const Node n = Node::fromJson(m.payload.value(key::kNode).toObject());
        if (!n.isValid() || n.id == d->self.id || d->store->getNavi(n.id, nullptr)) break;
        Node added = n;
        added.connected = true;
        d->store->upsertNavi(added);
        log(QStringLiteral("INFO"), QStringLiteral("membership"), QStringLiteral("%1 (%2) joined").arg(n.name, n.id));
        emit naviJoined(added);
        break;
    }
    case MessageType::NaviLeft:
        applyNaviLeft(m.payload.value(key::kNaviId).toString(m.senderId));
        break;
    case MessageType::FileUpload:
    case MessageType::FileUpdate:
        applyRemoteFile(FileMetadata::fromJson(m.payload.value(key::kFile).toObject()));
        break;
    case MessageType::FileDelete:
        applyRemoteDelete(m.payload.value(key::kFileId).toString());
        break;
    case MessageType::MetadataSync: {
        const QString kind = m.payload.value(key::kKind).toString();
        if (kind == sync::kSnapshotRequest) {
            send(MessageType::MetadataSync, {{key::kKind, QString(sync::kSnapshot)}, {key::kSnapshot, exportSnapshot()}}, m.senderId);
        } else if (kind == sync::kSnapshot) {
            const Result r = importSnapshot(m.payload.value(key::kSnapshot).toObject());
            if (!r) log(QStringLiteral("WARN"), QStringLiteral("sync"), QStringLiteral("Snapshot rejected: ") + r.error);
        } else if (kind == sync::kLogsRequest) {
            if (m.senderId != p.guardianId) {
                log(QStringLiteral("WARN"), QStringLiteral("protocol"), QStringLiteral("logs_request from non-Guardian ignored"));
                break;
            }
            QJsonArray arr;
            for (const LogEntry& e : d->store->logs())
                arr.append(QJsonObject{{"seq", static_cast<double>(e.seq)},
                                       {"timestampMs", static_cast<double>(e.timestampMs)},
                                       {"level", e.level},
                                       {"category", e.category},
                                       {"message", e.message}});
            send(MessageType::MetadataSync, {{key::kKind, QString(sync::kLogs)}, {key::kLogs, arr}}, m.senderId);
        } else if (kind == sync::kLogs) {
            Node sender;
            if (p.guardianId != d->self.id || !d->store->getNavi(m.senderId, &sender)) break;  // only the Guardian collects
            QList<LogEntry> entries;
            for (const QJsonValue& v : m.payload.value(key::kLogs).toArray()) {
                const QJsonObject o = v.toObject();
                LogEntry e;
                e.seq = static_cast<qint64>(o.value(QStringLiteral("seq")).toDouble());
                e.timestampMs = static_cast<qint64>(o.value(QStringLiteral("timestampMs")).toDouble());
                e.level = o.value(QStringLiteral("level")).toString();
                e.category = o.value(QStringLiteral("category")).toString();
                e.message = o.value(QStringLiteral("message")).toString();
                entries.append(e);
            }
            d->collectedLogs.insert(m.senderId, entries);
            emit logsReceived(m.senderId);
        } else if (kind == sync::kReplicaUpdate) {
            applyReplicaUpdate(m.payload.value(key::kFileId).toString(), toStringList(m.payload.value(key::kAddReplicas)),
                               toStringList(m.payload.value(key::kRemoveReplicas)));
        }
        break;
    }
    case MessageType::GuardianChanged:
        applyGuardianChanged(m);
        break;
    case MessageType::ShutdownWarning: {
        if (m.senderId != p.guardianId) {
            log(QStringLiteral("WARN"), QStringLiteral("protocol"), QStringLiteral("SHUTDOWN_WARNING from non-Guardian ignored"));
            break;
        }
        if (p.state == PandoraState::ShuttingDown) break;
        beginCountdown(m.payload.value(key::kSecondsRemaining).toInt(d->config.shutdownGraceSeconds));
        break;
    }
    case MessageType::PandoraDeactivate:
        if (m.senderId != p.guardianId) {
            log(QStringLiteral("WARN"), QStringLiteral("protocol"), QStringLiteral("PANDORA_DEACTIVATE from non-Guardian ignored"));
            break;
        }
        wipe(true);
        break;
    default:
        break;
    }
}

// ======================================================================== queries

bool MetadataService::isOpen() const {
    Lock lock(d->mutex);
    return d->store && d->store->isOpen();
}

bool MetadataService::hasPandora() const {
    Lock lock(d->mutex);
    return d->store && d->store->loadPandora(nullptr);
}

Node MetadataService::self() const { return d->self; }

Pandora MetadataService::pandora() const {
    Lock lock(d->mutex);
    Pandora p;
    if (d->store) d->store->loadPandora(&p);
    return p;
}

QString MetadataService::guardianId() const { return pandora().guardianId; }

bool MetadataService::isGuardian() const {
    const QString g = guardianId();
    return !g.isEmpty() && g == d->self.id;
}

QList<Node> MetadataService::listNavis(bool onlyConnected) const {
    Lock lock(d->mutex);
    return d->store ? d->store->navis(onlyConnected) : QList<Node>();
}

bool MetadataService::getNavi(const QString& naviId, Node* out) const {
    Lock lock(d->mutex);
    return d->store && d->store->getNavi(naviId, out);
}

QList<FileMetadata> MetadataService::listFiles() const {
    Lock lock(d->mutex);
    return d->store ? d->store->files() : QList<FileMetadata>();
}

bool MetadataService::getFile(const QString& fileId, FileMetadata* out) const {
    Lock lock(d->mutex);
    return d->store && d->store->getFile(fileId, out);
}

bool MetadataService::getFileByName(const QString& name, FileMetadata* out) const {
    Lock lock(d->mutex);
    return d->store && d->store->getFileByName(name, out);
}

QStringList MetadataService::availableReplicas(const QString& fileId) const {
    Lock lock(d->mutex);
    return d->store ? d->store->availableReplicas(fileId) : QStringList();
}

qint64 MetadataService::storageUsage(const QString& naviId) const {
    Lock lock(d->mutex);
    return d->store ? d->store->usage(naviId) : 0;
}

QMap<QString, qint64> MetadataService::storageUsageByNavi() const {
    Lock lock(d->mutex);
    QMap<QString, qint64> usage;
    if (d->store)
        for (const Node& n : d->store->navis(true)) usage.insert(n.id, n.storageUsedBytes);
    return usage;
}

}  // namespace pandora::metadata
