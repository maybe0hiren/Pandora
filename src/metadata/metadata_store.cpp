#include "metadata/metadata_store.h"
#include <QJsonArray>
#include <QMutexLocker>

namespace pandora {
MetadataStore::MetadataStore(QObject* parent) : QObject(parent) {}
void MetadataStore::reset() {
    { QMutexLocker l(&mutex_); nodes_.clear(); files_.clear(); guardian_.clear(); session_ = {}; }
    emit changed();
}
bool MetadataStore::addNode(const NodeInfo& n) {
    { QMutexLocker l(&mutex_); nodes_.insert(n.id.toString(QUuid::WithoutBraces), n); }
    emit changed(); return true;
}
void MetadataStore::removeNode(const QString& id) {
    { QMutexLocker l(&mutex_); nodes_.remove(id); for (auto it = files_.begin(); it != files_.end(); ++it) it->replicas.removeAll(id); }
    emit changed();
}
QList<NodeInfo> MetadataStore::nodes() const {
    QMutexLocker l(&mutex_); return nodes_.values();
}
bool MetadataStore::addFile(const FileInfo& f, QString* error) {
    QMutexLocker l(&mutex_);
    for (const auto& existing : files_) {
        if (!existing.tombstone && existing.name.compare(f.name, Qt::CaseInsensitive) == 0 && existing.id != f.id) {
            if (error) *error = "A file with this name already exists in Pandora."; return false;
        }
    }
    files_.insert(f.id.toString(QUuid::WithoutBraces), f);
    l.unlock(); emit changed(); return true;
}
bool MetadataStore::updateFile(const FileInfo& f, QString* error) {
    QMutexLocker l(&mutex_);
    auto it = files_.find(f.id.toString(QUuid::WithoutBraces));
    if (it == files_.end()) { if (error) *error = "File not found."; return false; }
    // Last-write-wins ordering: version, timestamp, then node UUID as deterministic tie breaker.
    const bool newer = f.version > it->version ||
        (f.version == it->version && (f.modifiedAt > it->modifiedAt ||
        (f.modifiedAt == it->modifiedAt && f.modifiedBy.toString() > it->modifiedBy.toString())));
    if (newer) *it = f;
    l.unlock(); emit changed(); return true;
}
bool MetadataStore::deleteFile(const QString& id, const QUuid& actor, QString* error) {
    QMutexLocker l(&mutex_);
    auto it = files_.find(id);
    if (it == files_.end()) { if (error) *error = "File not found."; return false; }
    it->tombstone = true; ++it->version; it->modifiedBy = actor; it->modifiedAt = QDateTime::currentDateTimeUtc();
    l.unlock(); emit changed(); return true;
}
QList<FileInfo> MetadataStore::files(bool includeDeleted) const {
    QMutexLocker l(&mutex_); QList<FileInfo> out;
    for (const auto& f : files_) if (includeDeleted || !f.tombstone) out.append(f);
    return out;
}
bool MetadataStore::applyRemoteFile(const FileInfo& f) {
    QMutexLocker l(&mutex_); const auto key = f.id.toString(QUuid::WithoutBraces);
    auto it = files_.find(key);
    if (it == files_.end()) files_.insert(key, f);
    else {
        const auto& old = *it;
        if (f.version > old.version || (f.version == old.version &&
            (f.modifiedAt > old.modifiedAt || (f.modifiedAt == old.modifiedAt &&
             f.modifiedBy.toString() > old.modifiedBy.toString())))) *it = f;
    }
    l.unlock(); emit changed(); return true;
}
void MetadataStore::setGuardian(const QString& id) { { QMutexLocker l(&mutex_); guardian_ = id; } emit changed(); }
QString MetadataStore::guardian() const { QMutexLocker l(&mutex_); return guardian_; }
void MetadataStore::setSession(const PandoraSession& s) { { QMutexLocker l(&mutex_); session_ = s; } emit changed(); }
PandoraSession MetadataStore::session() const { QMutexLocker l(&mutex_); return session_; }
QJsonObject MetadataStore::snapshot() const {
    QMutexLocker l(&mutex_); QJsonArray ns, fs;
    for (const auto& n : nodes_) ns.append(n.toJson());
    for (const auto& f : files_) fs.append(f.toJson());
    return {{"guardian", guardian_}, {"pandoraId", session_.id.toString(QUuid::WithoutBraces)},
            {"pandoraName", session_.name}, {"active", session_.active},
            {"nodes", ns}, {"files", fs}};
}
void MetadataStore::applySnapshot(const QJsonObject& j) {
    QMutexLocker l(&mutex_);
    guardian_ = j.value("guardian").toString();
    for (const auto& v : j.value("nodes").toArray()) { auto n = NodeInfo::fromJson(v.toObject()); nodes_.insert(n.id.toString(QUuid::WithoutBraces), n); }
    for (const auto& v : j.value("files").toArray()) {
        auto f = FileInfo::fromJson(v.toObject()); auto key = f.id.toString(QUuid::WithoutBraces);
        auto it = files_.find(key);
        if (it == files_.end() || f.version >= it->version) files_.insert(key, f);
    }
    l.unlock(); emit changed();
}
}
