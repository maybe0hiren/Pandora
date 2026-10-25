-- Pandora metadata schema (SQLite). One database per node, holding 100% of the metadata.
-- The database is temporary: it lives in memory (default) or in a temp file that is erased on deactivation.
-- NOTE: this file is embedded into the library at build time (see CMakeLists.txt). Only "--" comment
-- lines and ';'-terminated statements are allowed; do not use triggers (the splitter is naive).

PRAGMA foreign_keys = ON;

-- Exactly one row: the Pandora this node belongs to.
CREATE TABLE IF NOT EXISTS pandora (
    id                 TEXT PRIMARY KEY,
    key_hash           TEXT NOT NULL,              -- secure representation of the key, never the raw key
    guardian_id        TEXT NOT NULL,
    max_navi           INTEGER NOT NULL,
    created_at_ms      INTEGER NOT NULL,
    state              INTEGER NOT NULL DEFAULT 0, -- 0 Active, 1 ShuttingDown
    guardian_epoch     INTEGER NOT NULL DEFAULT 0
);

-- Na'vi registry. "connected" only ever goes 1 -> 0 (identities are temporary, a node never rejoins with the same id).
CREATE TABLE IF NOT EXISTS navi (
    id                 TEXT PRIMARY KEY,
    name               TEXT NOT NULL,
    address            TEXT NOT NULL DEFAULT '',
    port               INTEGER NOT NULL DEFAULT 0,
    connected          INTEGER NOT NULL DEFAULT 1,
    joined_at_ms       INTEGER NOT NULL
);

-- One row per live file (metadata only).
CREATE TABLE IF NOT EXISTS file (
    file_id            TEXT PRIMARY KEY,
    name               TEXT NOT NULL UNIQUE,       -- duplicate file names are not allowed
    size_bytes         INTEGER NOT NULL DEFAULT 0,
    version            INTEGER NOT NULL DEFAULT 1,
    modified_at_ms     INTEGER NOT NULL DEFAULT 0,
    modified_by        TEXT NOT NULL DEFAULT '',   -- node UUID, Last-Write-Wins tie breaker
    checksum           TEXT NOT NULL DEFAULT ''
);

-- Replica locations: which Na'vi stores which file.
CREATE TABLE IF NOT EXISTS replica (
    file_id            TEXT NOT NULL REFERENCES file(file_id) ON DELETE CASCADE,
    navi_id            TEXT NOT NULL REFERENCES navi(id)      ON DELETE CASCADE,
    PRIMARY KEY (file_id, navi_id)
);
CREATE INDEX IF NOT EXISTS idx_replica_navi ON replica(navi_id);

-- Deleted files. Deletion is final: late FILE_UPLOAD / FILE_UPDATE events for these ids are ignored.
CREATE TABLE IF NOT EXISTS tombstone (
    file_id            TEXT PRIMARY KEY,
    name               TEXT NOT NULL,
    deleted_at_ms      INTEGER NOT NULL
);

-- Idempotency: message ids already applied (events may be delivered twice, e.g. broadcast + snapshot).
CREATE TABLE IF NOT EXISTS applied_event (
    message_id         TEXT PRIMARY KEY,
    applied_at_ms      INTEGER NOT NULL
);

-- Local audit log shown to the Guardian.
CREATE TABLE IF NOT EXISTS event_log (
    seq                INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_ms              INTEGER NOT NULL,
    level              TEXT NOT NULL,              -- INFO | WARN | ERROR
    category           TEXT NOT NULL,
    message            TEXT NOT NULL
);

-- Storage usage per Na'vi, derived from replica locations (always consistent, nothing to keep in sync).
CREATE VIEW IF NOT EXISTS v_navi_usage AS
    SELECT n.id AS navi_id,
           COALESCE(SUM(f.size_bytes), 0) AS used_bytes,
           COUNT(f.file_id)               AS file_count
      FROM navi n
      LEFT JOIN replica r ON r.navi_id = n.id
      LEFT JOIN file f    ON f.file_id = r.file_id
     GROUP BY n.id;
