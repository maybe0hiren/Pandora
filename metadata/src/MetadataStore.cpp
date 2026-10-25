#include "MetadataStore.h"

#include <QDateTime>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

using pandora::shared::FileMetadata;
using pandora::shared::Node;
using pandora::shared::Pandora;
using pandora::shared::PandoraState;

namespace pandora::metadata {

namespace {
bool exec(QSqlQuery& q, QString* err) {
    if (q.exec()) return true;
    if (err) *err = q.lastError().text();
    return false;
}

Node nodeFromQuery(const QSqlQuery& q) {
    Node n;
    n.id = q.value(0).toString();
    n.name = q.value(1).toString();
    n.address = q.value(2).toString();
    n.port = static_cast<quint16>(q.value(3).toInt());
    n.connected = q.value(4).toInt() != 0;
    n.joinedAtMs = q.value(5).toLongLong();
    return n;
}

FileMetadata fileFromQuery(const QSqlQuery& q) {
    FileMetadata f;
    f.fileId = q.value(0).toString();
    f.name = q.value(1).toString();
    f.sizeBytes = q.value(2).toLongLong();
    f.version = q.value(3).toLongLong();
    f.modifiedAtMs = q.value(4).toLongLong();
    f.modifiedBy = q.value(5).toString();
    f.checksum = q.value(6).toString();
    return f;
}

/// A null QString binds as SQL NULL and would violate NOT NULL columns; bind it as '' instead.
QVariant text(const QString& s) { return s.isNull() ? QVariant(QStringLiteral("")) : QVariant(s); }

const char* kFileColumns = "file_id, name, size_bytes, version, modified_at_ms, modified_by, checksum";
}  // namespace

MetadataStore::MetadataStore() = default;

MetadataStore::~MetadataStore() {
    if (isOpen()) {
        {
            QSqlDatabase d = QSqlDatabase::database(m_connection, false);
            d.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
    }
}

QSqlDatabase MetadataStore::db() const { return QSqlDatabase::database(m_connection, false); }

bool MetadataStore::isOpen() const { return !m_connection.isEmpty() && db().isOpen(); }

bool MetadataStore::open(const QString& path, QString* error) {
    m_connection = QStringLiteral("pandora_meta_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_path = path;
    {
        QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        d.setDatabaseName(path);
        if (!d.open()) {
            if (error) *error = d.lastError().text();
        }
    }
    if (!QSqlDatabase::database(m_connection, false).isOpen()) {
        QSqlDatabase::removeDatabase(m_connection);
        m_connection.clear();
        return false;
    }
    if (!runScript(QString::fromUtf8(embeddedSchemaSql()))) {
        if (error) *error = m_lastError;
        return false;
    }
    return true;
}

bool MetadataStore::runScript(const QString& script) {
    QStringList lines;
    for (const QString& line : script.split(QLatin1Char('\n'))) {
        if (line.trimmed().startsWith(QLatin1String("--"))) continue;
        lines << line;
    }
    const QStringList statements = lines.join(QLatin1Char('\n')).split(QLatin1Char(';'));
    for (const QString& s : statements) {
        if (s.trimmed().isEmpty()) continue;
        QSqlQuery q(db());
        if (!q.exec(s)) {
            m_lastError = q.lastError().text() + QStringLiteral(" in: ") + s.trimmed();
            return false;
        }
    }
    return true;
}

void MetadataStore::closeAndErase() {
    if (!m_connection.isEmpty()) {
        {
            QSqlDatabase d = QSqlDatabase::database(m_connection, false);
            d.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
        m_connection.clear();
    }
    if (!m_path.isEmpty() && m_path != QLatin1String(":memory:")) {
        for (const QString& suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")})
            QFile::remove(m_path + suffix);
    }
    m_path.clear();
}

// ---------------------------------------------------------------- pandora

bool MetadataStore::savePandora(const Pandora& p) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO pandora (id, key_hash, guardian_id, max_navi, created_at_ms, state, guardian_epoch) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(p.id);
    q.addBindValue(text(p.keyHash));
    q.addBindValue(p.guardianId);
    q.addBindValue(p.maxNavi);
    q.addBindValue(p.createdAtMs);
    q.addBindValue(static_cast<int>(p.state));
    q.addBindValue(p.guardianEpoch);
    return exec(q, &m_lastError);
}

bool MetadataStore::loadPandora(Pandora* out) const {
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral(
            "SELECT id, key_hash, guardian_id, max_navi, created_at_ms, state, guardian_epoch FROM pandora LIMIT 1")) ||
        !q.next())
        return false;
    Pandora p;
    p.id = q.value(0).toString();
    p.keyHash = q.value(1).toString();
    p.guardianId = q.value(2).toString();
    p.maxNavi = q.value(3).toInt();
    p.createdAtMs = q.value(4).toLongLong();
    p.state = static_cast<PandoraState>(q.value(5).toInt());
    p.guardianEpoch = q.value(6).toLongLong();
    if (out) *out = p;
    return true;
}

// ---------------------------------------------------------------- navi

bool MetadataStore::upsertNavi(const Node& n) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO navi (id, name, address, port, connected, joined_at_ms) VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name, address = excluded.address, port = excluded.port"));
    q.addBindValue(n.id);
    q.addBindValue(text(n.name));
    q.addBindValue(text(n.address));
    q.addBindValue(static_cast<int>(n.port));
    q.addBindValue(n.connected ? 1 : 0);
    q.addBindValue(n.joinedAtMs);
    return exec(q, &m_lastError);
}

bool MetadataStore::setNaviConnected(const QString& id, bool connected) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE navi SET connected = ? WHERE id = ?"));
    q.addBindValue(connected ? 1 : 0);
    q.addBindValue(id);
    return exec(q, &m_lastError);
}

bool MetadataStore::getNavi(const QString& id, Node* out) const {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, name, address, port, connected, joined_at_ms FROM navi WHERE id = ?"));
    q.addBindValue(id);
    if (!exec(q, &m_lastError) || !q.next()) return false;
    if (out) {
        *out = nodeFromQuery(q);
        out->storageUsedBytes = usage(id);
    }
    return true;
}

QList<Node> MetadataStore::navis(bool onlyConnected) const {
    QList<Node> list;
    QSqlQuery q(db());
    QString sql = QStringLiteral(
        "SELECT n.id, n.name, n.address, n.port, n.connected, n.joined_at_ms, u.used_bytes "
        "FROM navi n JOIN v_navi_usage u ON u.navi_id = n.id ");
    if (onlyConnected) sql += QStringLiteral("WHERE n.connected = 1 ");
    sql += QStringLiteral("ORDER BY n.joined_at_ms, n.id");
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        return list;
    }
    while (q.next()) {
        Node n = nodeFromQuery(q);
        n.storageUsedBytes = q.value(6).toLongLong();
        list << n;
    }
    return list;
}

int MetadataStore::connectedCount() const {
    QSqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM navi WHERE connected = 1")) && q.next()) return q.value(0).toInt();
    return 0;
}

// ---------------------------------------------------------------- files

bool MetadataStore::upsertFile(const FileMetadata& f) {
    QSqlDatabase d = db();
    d.transaction();
    bool ok = true;
    {
        QSqlQuery q(d);
        q.prepare(QStringLiteral(
            "INSERT INTO file (file_id, name, size_bytes, version, modified_at_ms, modified_by, checksum) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(file_id) DO UPDATE SET name = excluded.name, size_bytes = excluded.size_bytes, "
            "version = excluded.version, modified_at_ms = excluded.modified_at_ms, "
            "modified_by = excluded.modified_by, checksum = excluded.checksum"));
        q.addBindValue(f.fileId);
        q.addBindValue(f.name);
        q.addBindValue(f.sizeBytes);
        q.addBindValue(f.version);
        q.addBindValue(f.modifiedAtMs);
        q.addBindValue(text(f.modifiedBy));
        q.addBindValue(text(f.checksum));
        ok = exec(q, &m_lastError);
    }
    if (ok) {
        QSqlQuery del(d);
        del.prepare(QStringLiteral("DELETE FROM replica WHERE file_id = ?"));
        del.addBindValue(f.fileId);
        ok = exec(del, &m_lastError);
    }
    if (ok) {
        for (const QString& naviId : f.replicaNodeIds) {
            // INSERT ... SELECT skips Na'vi we do not know (avoids a foreign-key failure on stale ids).
            QSqlQuery q(d);
            q.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO replica (file_id, navi_id) SELECT ?, id FROM navi WHERE id = ?"));
            q.addBindValue(f.fileId);
            q.addBindValue(naviId);
            if (!exec(q, &m_lastError)) {
                ok = false;
                break;
            }
        }
    }
    if (ok) return d.commit();
    d.rollback();
    return false;
}

bool MetadataStore::deleteFile(const QString& fileId) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM file WHERE file_id = ?"));  // replicas cascade
    q.addBindValue(fileId);
    return exec(q, &m_lastError);
}

bool MetadataStore::getFile(const QString& fileId, FileMetadata* out) const {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT %1 FROM file WHERE file_id = ?").arg(QLatin1String(kFileColumns)));
    q.addBindValue(fileId);
    if (!exec(q, &m_lastError) || !q.next()) return false;
    if (out) {
        *out = fileFromQuery(q);
        QSqlQuery r(db());
        r.prepare(QStringLiteral("SELECT navi_id FROM replica WHERE file_id = ? ORDER BY navi_id"));
        r.addBindValue(fileId);
        if (r.exec())
            while (r.next()) out->replicaNodeIds << r.value(0).toString();
    }
    return true;
}

bool MetadataStore::getFileByName(const QString& name, FileMetadata* out) const {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT file_id FROM file WHERE name = ?"));
    q.addBindValue(text(name));
    if (!exec(q, &m_lastError) || !q.next()) return false;
    return getFile(q.value(0).toString(), out);
}

QList<FileMetadata> MetadataStore::files() const {
    QList<FileMetadata> list;
    QStringList ids;
    QSqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT file_id FROM file ORDER BY name")))
        while (q.next()) ids << q.value(0).toString();
    for (const QString& id : ids) {
        FileMetadata f;
        if (getFile(id, &f)) list << f;
    }
    return list;
}

bool MetadataStore::addReplica(const QString& fileId, const QString& naviId) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO replica (file_id, navi_id) "
        "SELECT f.file_id, n.id FROM file f, navi n WHERE f.file_id = ? AND n.id = ?"));
    q.addBindValue(fileId);
    q.addBindValue(naviId);
    return exec(q, &m_lastError);
}

bool MetadataStore::removeReplica(const QString& fileId, const QString& naviId) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM replica WHERE file_id = ? AND navi_id = ?"));
    q.addBindValue(fileId);
    q.addBindValue(naviId);
    return exec(q, &m_lastError);
}

QStringList MetadataStore::removeReplicasOfNavi(const QString& naviId) {
    QStringList affected;
    {
        QSqlQuery q(db());
        q.prepare(QStringLiteral("SELECT file_id FROM replica WHERE navi_id = ?"));
        q.addBindValue(naviId);
        if (exec(q, &m_lastError))
            while (q.next()) affected << q.value(0).toString();
    }
    QSqlQuery del(db());
    del.prepare(QStringLiteral("DELETE FROM replica WHERE navi_id = ?"));
    del.addBindValue(naviId);
    exec(del, &m_lastError);
    return affected;
}

QStringList MetadataStore::availableReplicas(const QString& fileId) const {
    QStringList ids;
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT r.navi_id FROM replica r JOIN navi n ON n.id = r.navi_id "
        "WHERE r.file_id = ? AND n.connected = 1 ORDER BY r.navi_id"));
    q.addBindValue(fileId);
    if (exec(q, &m_lastError))
        while (q.next()) ids << q.value(0).toString();
    return ids;
}

qint64 MetadataStore::usage(const QString& naviId) const {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT used_bytes FROM v_navi_usage WHERE navi_id = ?"));
    q.addBindValue(naviId);
    if (exec(q, &m_lastError) && q.next()) return q.value(0).toLongLong();
    return 0;
}

// ---------------------------------------------------------------- tombstones

bool MetadataStore::addTombstone(const QString& fileId, const QString& name, qint64 deletedAtMs) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO tombstone (file_id, name, deleted_at_ms) VALUES (?, ?, ?)"));
    q.addBindValue(fileId);
    q.addBindValue(text(name));
    q.addBindValue(deletedAtMs);
    return exec(q, &m_lastError);
}

bool MetadataStore::isTombstoned(const QString& fileId) const {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT 1 FROM tombstone WHERE file_id = ?"));
    q.addBindValue(fileId);
    return exec(q, &m_lastError) && q.next();
}

QList<QPair<QString, QString>> MetadataStore::tombstones() const {
    QList<QPair<QString, QString>> list;
    QSqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT file_id, name FROM tombstone")))
        while (q.next()) list.append({q.value(0).toString(), q.value(1).toString()});
    return list;
}

// ---------------------------------------------------------------- idempotency

bool MetadataStore::wasApplied(const QString& messageId) const {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT 1 FROM applied_event WHERE message_id = ?"));
    q.addBindValue(messageId);
    return exec(q, &m_lastError) && q.next();
}

bool MetadataStore::markApplied(const QString& messageId) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO applied_event (message_id, applied_at_ms) VALUES (?, ?)"));
    q.addBindValue(messageId);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    return exec(q, &m_lastError);
}

// ---------------------------------------------------------------- logs

void MetadataStore::appendLog(const QString& level, const QString& category, const QString& message) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO event_log (ts_ms, level, category, message) VALUES (?, ?, ?, ?)"));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(text(level));
    q.addBindValue(text(category));
    q.addBindValue(text(message));
    exec(q, &m_lastError);
}

QList<LogEntry> MetadataStore::logs() const {
    QList<LogEntry> list;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT seq, ts_ms, level, category, message FROM event_log ORDER BY seq"))) return list;
    while (q.next()) {
        LogEntry e;
        e.seq = q.value(0).toLongLong();
        e.timestampMs = q.value(1).toLongLong();
        e.level = q.value(2).toString();
        e.category = q.value(3).toString();
        e.message = q.value(4).toString();
        list << e;
    }
    return list;
}

}  // namespace pandora::metadata
