#include "metadata/metadata_store.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>

namespace pandora {

MetadataStore::MetadataStore(QObject* parent) : QObject(parent) {}

MetadataStore::~MetadataStore() {
    reset();
}

bool MetadataStore::initializePersistence(const QString& sqliteFilePath) {
    QMutexLocker locker(&mutex_);
    if (dbOpen_) {
        QSqlDatabase::database(dbConnectionName_).close();
        QSqlDatabase::removeDatabase(dbConnectionName_);
        dbOpen_ = false;
    }
    dbConnectionName_ = "pandora_meta_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", dbConnectionName_);
    db.setDatabaseName(sqliteFilePath);
    if (!db.open()) {
        return false;
    }
    dbOpen_ = true;

    QSqlQuery q(db);
    q.exec("PRAGMA journal_mode=WAL;");
    q.exec("CREATE TABLE IF NOT EXISTS files (id TEXT PRIMARY KEY, json TEXT NOT NULL);");
    q.exec("CREATE TABLE IF NOT EXISTS nodes (id TEXT PRIMARY KEY, json TEXT NOT NULL);");
    q.exec("CREATE TABLE IF NOT EXISTS events (id TEXT PRIMARY KEY, timestamp TEXT, json TEXT NOT NULL);");
    return true;
}

void MetadataStore::reset() {
    {
        QMutexLocker l(&mutex_);
        nodes_.clear();
        files_.clear();
        seenEvents_.clear();
        eventHistory_.clear();
        guardian_.clear();
        session_ = {};
        if (dbOpen_) {
            {
                QSqlDatabase db = QSqlDatabase::database(dbConnectionName_);
                db.close();
            }
            QSqlDatabase::removeDatabase(dbConnectionName_);
            dbOpen_ = false;
        }
    }
    emit changed();
}

bool MetadataStore::isFileNewer(const FileInfo& incoming, const FileInfo& existing) {
    // Compare version vectors first if both have entries
    if (!incoming.versionVector.isEmpty() && !existing.versionVector.isEmpty()) {
        bool incomingDominates = false;
        bool existingDominates = false;
        QSet<QString> allKeys;
        for (auto it = incoming.versionVector.begin(); it != incoming.versionVector.end(); ++it) allKeys.insert(it.key());
        for (auto it = existing.versionVector.begin(); it != existing.versionVector.end(); ++it) allKeys.insert(it.key());
        for (const QString& k : allKeys) {
            const quint64 vIn = incoming.versionVector.value(k, 0);
            const quint64 vEx = existing.versionVector.value(k, 0);
            if (vIn > vEx) incomingDominates = true;
            if (vEx > vIn) existingDominates = true;
        }
        if (incomingDominates && !existingDominates) return true;
        if (existingDominates && !incomingDominates) return false;
    }

    // Tombstone never loses to equal version non-tombstone (prevents resurrecting deleted files)
    if (incoming.version == existing.version && incoming.tombstone != existing.tombstone) {
        return incoming.tombstone;
    }

    // Deterministic tie-breaker: version -> UTC timestamp -> modifiedBy UUID string
    if (incoming.version != existing.version) {
        return incoming.version > existing.version;
    }
    if (incoming.modifiedAt != existing.modifiedAt) {
        return incoming.modifiedAt > existing.modifiedAt;
    }
    return incoming.modifiedBy.toString(QUuid::WithoutBraces) > existing.modifiedBy.toString(QUuid::WithoutBraces);
}

void MetadataStore::persistFileUnlocked(const FileInfo& f) {
    if (!dbOpen_) return;
    QSqlDatabase db = QSqlDatabase::database(dbConnectionName_);
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO files (id, json) VALUES (:id, :json);");
    q.bindValue(":id", f.id.toString(QUuid::WithoutBraces));
    q.bindValue(":json", QString::fromUtf8(QJsonDocument(f.toJson()).toJson(QJsonDocument::Compact)));
    q.exec();
}

void MetadataStore::persistNodeUnlocked(const NodeInfo& n) {
    if (!dbOpen_) return;
    QSqlDatabase db = QSqlDatabase::database(dbConnectionName_);
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO nodes (id, json) VALUES (:id, :json);");
    q.bindValue(":id", n.id.toString(QUuid::WithoutBraces));
    q.bindValue(":json", QString::fromUtf8(QJsonDocument(n.toJson()).toJson(QJsonDocument::Compact)));
    q.exec();
}

void MetadataStore::persistEventUnlocked(const PandoraEvent& e) {
    if (!dbOpen_) return;
    QSqlDatabase db = QSqlDatabase::database(dbConnectionName_);
    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO events (id, timestamp, json) VALUES (:id, :ts, :json);");
    q.bindValue(":id", e.eventId.toString(QUuid::WithoutBraces));
    q.bindValue(":ts", e.timestamp.toUTC().toString(Qt::ISODateWithMs));
    q.bindValue(":json", QString::fromUtf8(QJsonDocument(e.toJson()).toJson(QJsonDocument::Compact)));
    q.exec();
}

bool MetadataStore::addNode(const NodeInfo& n) {
    {
        QMutexLocker l(&mutex_);
        const QString id = n.id.toString(QUuid::WithoutBraces);
        NodeInfo merged = n;
        if (!merged.joinedAt.isValid()) {
            merged.joinedAt = nodes_.contains(id) ? nodes_.value(id).joinedAt : QDateTime::currentDateTimeUtc();
        }
        if (!merged.lastSeenUtc.isValid()) {
            merged.lastSeenUtc = QDateTime::currentDateTimeUtc();
        }
        nodes_.insert(id, merged);
        persistNodeUnlocked(merged);
    }
    emit changed();
    return true;
}

bool MetadataStore::updateNodeStatus(const QString& nodeId, bool connected, qint64 managedBytes, qint64 availableDiskBytes) {
    bool modified = false;
    {
        QMutexLocker l(&mutex_);
        auto it = nodes_.find(nodeId);
        if (it != nodes_.end()) {
            it->connected = connected;
            it->lastSeenUtc = QDateTime::currentDateTimeUtc();
            if (managedBytes >= 0) it->managedBytes = managedBytes;
            if (availableDiskBytes >= 0) it->availableDiskBytes = availableDiskBytes;
            if (!connected) {
                for (auto fit = files_.begin(); fit != files_.end(); ++fit) {
                    fit->replicas.removeAll(nodeId);
                }
            }
            persistNodeUnlocked(*it);
            modified = true;
        }
    }
    if (modified) emit changed();
    return modified;
}

void MetadataStore::removeNode(const QString& id) {
    {
        QMutexLocker l(&mutex_);
        nodes_.remove(id);
        for (auto it = files_.begin(); it != files_.end(); ++it) {
            it->replicas.removeAll(id);
            persistFileUnlocked(*it);
        }
    }
    emit changed();
}

NodeInfo MetadataStore::node(const QString& nodeId) const {
    QMutexLocker l(&mutex_);
    return nodes_.value(nodeId);
}

QList<NodeInfo> MetadataStore::nodes(bool connectedOnly) const {
    QMutexLocker l(&mutex_);
    if (!connectedOnly) return nodes_.values();
    QList<NodeInfo> out;
    for (const auto& n : nodes_) {
        if (n.connected) out.append(n);
    }
    return out;
}

bool MetadataStore::addFile(const FileInfo& f, QString* error) {
    QMutexLocker l(&mutex_);
    for (const auto& existing : files_) {
        if (!existing.tombstone && existing.name.compare(f.name, Qt::CaseInsensitive) == 0 && existing.id != f.id) {
            if (error) *error = QString("A file named '%1' already exists in Pandora.").arg(f.name);
            return false;
        }
    }
    FileInfo copy = f;
    const QString actorStr = copy.modifiedBy.toString(QUuid::WithoutBraces);
    if (!actorStr.isEmpty()) {
        copy.versionVector[actorStr] = copy.versionVector.value(actorStr, 0) + 1;
    }
    const QString key = copy.id.toString(QUuid::WithoutBraces);
    files_.insert(key, copy);
    persistFileUnlocked(copy);
    l.unlock();
    emit changed();
    return true;
}

bool MetadataStore::updateFile(const FileInfo& f, QString* error) {
    QMutexLocker l(&mutex_);
    const QString key = f.id.toString(QUuid::WithoutBraces);
    auto it = files_.find(key);
    if (it == files_.end()) {
        if (error) *error = "File not found.";
        return false;
    }
    if (isFileNewer(f, *it)) {
        *it = f;
        persistFileUnlocked(*it);
    }
    l.unlock();
    emit changed();
    return true;
}

bool MetadataStore::deleteFile(const QString& id, const QUuid& actor, QString* error) {
    QMutexLocker l(&mutex_);
    auto it = files_.find(id);
    if (it == files_.end()) {
        if (error) *error = "File not found.";
        return false;
    }
    it->tombstone = true;
    ++it->version;
    it->modifiedBy = actor;
    it->modifiedAt = QDateTime::currentDateTimeUtc();
    it->replicas.clear();
    const QString actorStr = actor.toString(QUuid::WithoutBraces);
    if (!actorStr.isEmpty()) {
        it->versionVector[actorStr] = it->versionVector.value(actorStr, 0) + 1;
    }
    persistFileUnlocked(*it);
    l.unlock();
    emit changed();
    return true;
}

bool MetadataStore::addReplicaToFile(const QString& fileId, const QString& nodeId) {
    if (nodeId.isEmpty()) return false;
    bool changedFlag = false;
    {
        QMutexLocker l(&mutex_);
        auto it = files_.find(fileId);
        if (it != files_.end() && !it->tombstone && !it->replicas.contains(nodeId)) {
            it->replicas.append(nodeId);
            persistFileUnlocked(*it);
            changedFlag = true;
        }
    }
    if (changedFlag) emit changed();
    return changedFlag;
}

bool MetadataStore::removeReplicaFromFile(const QString& fileId, const QString& nodeId) {
    bool changedFlag = false;
    {
        QMutexLocker l(&mutex_);
        auto it = files_.find(fileId);
        if (it != files_.end() && it->replicas.removeAll(nodeId) > 0) {
            persistFileUnlocked(*it);
            changedFlag = true;
        }
    }
    if (changedFlag) emit changed();
    return changedFlag;
}

FileInfo MetadataStore::file(const QString& fileId, bool* found) const {
    QMutexLocker l(&mutex_);
    auto it = files_.find(fileId);
    if (it == files_.end()) {
        if (found) *found = false;
        return {};
    }
    if (found) *found = true;
    return *it;
}

QList<FileInfo> MetadataStore::files(bool includeDeleted) const {
    QMutexLocker l(&mutex_);
    QList<FileInfo> out;
    for (const auto& f : files_) {
        if (includeDeleted || !f.tombstone) out.append(f);
    }
    std::sort(out.begin(), out.end(), [](const FileInfo& a, const FileInfo& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

bool MetadataStore::applyRemoteFile(const FileInfo& f) {
    QMutexLocker l(&mutex_);
    const auto key = f.id.toString(QUuid::WithoutBraces);
    auto it = files_.find(key);
    if (it == files_.end()) {
        // Check for concurrent duplicate filename collision from different nodes
        if (!f.tombstone) {
            for (auto existingIt = files_.begin(); existingIt != files_.end(); ++existingIt) {
                if (!existingIt->tombstone && existingIt->id != f.id &&
                    existingIt->name.compare(f.name, Qt::CaseInsensitive) == 0) {
                    // Deterministic conflict preservation: rename newer/tie-losing file with node suffix
                    FileInfo renamed = f;
                    const int dotIdx = renamed.name.lastIndexOf('.');
                    const QString shortNode = f.modifiedBy.toString(QUuid::WithoutBraces).left(6);
                    if (dotIdx > 0) {
                        renamed.name = renamed.name.left(dotIdx) + "_conflict_" + shortNode + renamed.name.mid(dotIdx);
                    } else {
                        renamed.name = renamed.name + "_conflict_" + shortNode;
                    }
                    files_.insert(key, renamed);
                    persistFileUnlocked(renamed);
                    l.unlock();
                    emit changed();
                    return true;
                }
            }
        }
        files_.insert(key, f);
        persistFileUnlocked(f);
    } else {
        // Merge replicas if same version & not tombstoned, or overwrite if strictly newer
        if (!f.tombstone && !it->tombstone && f.version == it->version && f.sha256 == it->sha256) {
            for (const QString& r : f.replicas) {
                if (!it->replicas.contains(r)) it->replicas.append(r);
            }
            persistFileUnlocked(*it);
        } else if (isFileNewer(f, *it)) {
            *it = f;
            persistFileUnlocked(*it);
        }
    }
    l.unlock();
    emit changed();
    return true;
}

void MetadataStore::setGuardian(const QString& id, quint64 term) {
    {
        QMutexLocker l(&mutex_);
        if (term > 0 && term < session_.term) return;
        guardian_ = id;
        session_.guardianNodeId = id;
        if (term > session_.term) session_.term = term;
    }
    emit changed();
}

QString MetadataStore::guardian() const {
    QMutexLocker l(&mutex_);
    return guardian_;
}

quint64 MetadataStore::term() const {
    QMutexLocker l(&mutex_);
    return session_.term;
}

QString MetadataStore::electDeterministicGuardian() const {
    QMutexLocker l(&mutex_);
    QList<NodeInfo> connectedNodes;
    for (const auto& n : nodes_) {
        if (n.connected) connectedNodes.append(n);
    }
    if (connectedNodes.isEmpty()) return {};

    // Deterministic leader election: oldest joinedAt timestamp wins; tie-breaker is lowest lexicographical UUID
    std::sort(connectedNodes.begin(), connectedNodes.end(), [](const NodeInfo& a, const NodeInfo& b) {
        if (a.joinedAt.isValid() && b.joinedAt.isValid() && a.joinedAt != b.joinedAt) {
            return a.joinedAt < b.joinedAt;
        }
        return a.id.toString(QUuid::WithoutBraces) < b.id.toString(QUuid::WithoutBraces);
    });
    return connectedNodes.first().id.toString(QUuid::WithoutBraces);
}

void MetadataStore::setSession(const PandoraSession& s) {
    {
        QMutexLocker l(&mutex_);
        session_ = s;
        if (!s.guardianNodeId.isEmpty()) guardian_ = s.guardianNodeId;
    }
    emit changed();
}

PandoraSession MetadataStore::session() const {
    QMutexLocker l(&mutex_);
    return session_;
}

bool MetadataStore::hasSeenEvent(const QUuid& eventId) const {
    QMutexLocker l(&mutex_);
    return seenEvents_.contains(eventId.toString(QUuid::WithoutBraces));
}

bool MetadataStore::recordEvent(const PandoraEvent& event) {
    QMutexLocker l(&mutex_);
    const QString id = event.eventId.toString(QUuid::WithoutBraces);
    if (seenEvents_.contains(id)) return false;
    seenEvents_.insert(id);
    eventHistory_.append(event);
    while (eventHistory_.size() > 500) {
        eventHistory_.removeFirst();
    }
    persistEventUnlocked(event);
    return true;
}

QList<PandoraEvent> MetadataStore::recentEvents(int limit) const {
    QMutexLocker l(&mutex_);
    if (eventHistory_.size() <= limit) return eventHistory_;
    return eventHistory_.mid(eventHistory_.size() - limit);
}

QJsonObject MetadataStore::snapshot() const {
    QMutexLocker l(&mutex_);
    QJsonArray ns, fs;
    for (const auto& n : nodes_) ns.append(n.toJson());
    for (const auto& f : files_) fs.append(f.toJson());
    return {
        {"guardian", guardian_},
        {"session", session_.toJson()},
        {"pandoraId", session_.id.toString(QUuid::WithoutBraces)},
        {"pandoraName", session_.name},
        {"active", session_.active},
        {"nodes", ns},
        {"files", fs}
    };
}

void MetadataStore::applySnapshot(const QJsonObject& j) {
    QMutexLocker l(&mutex_);
    if (j.contains("session")) {
        const PandoraSession remoteSession = PandoraSession::fromJson(j.value("session").toObject());
        if (!session_.active || remoteSession.term >= session_.term) {
            // Preserve local keyVerifier if remote didn't send it
            const QByteArray localVerifier = session_.keyVerifier;
            const QByteArray localSalt = session_.salt;
            session_ = remoteSession;
            if (session_.keyVerifier.isEmpty()) session_.keyVerifier = localVerifier;
            if (session_.salt.isEmpty()) session_.salt = localSalt;
        }
    } else {
        if (!j.value("pandoraId").toString().isEmpty()) {
            session_.id = QUuid(j.value("pandoraId").toString());
        }
        if (!j.value("pandoraName").toString().isEmpty()) {
            session_.name = j.value("pandoraName").toString();
        }
        session_.active = j.value("active").toBool(true);
    }

    const QString snapGuardian = j.value("guardian").toString();
    if (!snapGuardian.isEmpty()) {
        guardian_ = snapGuardian;
        session_.guardianNodeId = snapGuardian;
    }

    for (const auto& v : j.value("nodes").toArray()) {
        auto n = NodeInfo::fromJson(v.toObject());
        const QString nid = n.id.toString(QUuid::WithoutBraces);
        if (!nid.isEmpty()) {
            nodes_.insert(nid, n);
            persistNodeUnlocked(n);
        }
    }

    for (const auto& v : j.value("files").toArray()) {
        auto f = FileInfo::fromJson(v.toObject());
        const auto key = f.id.toString(QUuid::WithoutBraces);
        if (key.isEmpty()) continue;
        auto it = files_.find(key);
        if (it == files_.end()) {
            files_.insert(key, f);
            persistFileUnlocked(f);
        } else if (isFileNewer(f, *it)) {
            *it = f;
            persistFileUnlocked(*it);
        } else if (!f.tombstone && !it->tombstone && f.version == it->version && f.sha256 == it->sha256) {
            for (const QString& r : f.replicas) {
                if (!it->replicas.contains(r)) it->replicas.append(r);
            }
            persistFileUnlocked(*it);
        }
    }
    l.unlock();
    emit changed();
}

} // namespace pandora
