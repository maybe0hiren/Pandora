# Metadata database design

One SQLite database per node; every node holds all of it. Source of truth: [sql/schema.sql](../sql/schema.sql),
embedded into the library at build time. Default location is `:memory:`; a file path is erased (with `-wal`, `-shm`,
`-journal`) on deactivation.

```mermaid
erDiagram
    PANDORA ||--o{ NAVI : "has members"
    NAVI ||--o{ REPLICA : "stores"
    FILE ||--o{ REPLICA : "has"
    PANDORA { text id PK  text key_hash  text guardian_id  int max_navi  int state  int guardian_epoch }
    NAVI { text id PK  text name  text address  int port  int connected  int joined_at_ms }
    FILE { text file_id PK  text name UK  int size_bytes  int version  int modified_at_ms  text modified_by  text checksum }
    REPLICA { text file_id PK,FK  text navi_id PK,FK }
    TOMBSTONE { text file_id PK  text name  int deleted_at_ms }
    APPLIED_EVENT { text message_id PK  int applied_at_ms }
    EVENT_LOG { int seq PK  int ts_ms  text level  text category  text message }
```

| Table | Purpose |
|---|---|
| `pandora` | Single row: ID, **hash** of the key (never the raw key), Guardian, capacity, state (0 Active, 1 ShuttingDown), Guardian epoch |
| `navi` | Na'vi registry with `connected` flag (only ever 1 -> 0) |
| `file` | File metadata; `name` is UNIQUE (no duplicate names); `version` + `modified_at_ms` + `modified_by` implement Last Write Wins |
| `replica` | Replica locations; deleting a file cascades, deleting a Na'vi's replicas happens when it leaves |
| `tombstone` | Deleted file ids, so a late upload/edit cannot resurrect a file |
| `applied_event` | Message ids already applied (idempotency) |
| `event_log` | Audit log that the Guardian can view; returned as final logs on deactivation |
| `v_navi_usage` (view) | Bytes and file count per Na'vi, computed from `replica` JOIN `file`; basis for least-loaded replica selection |

Rules enforced in code: `connected` never goes back to 1; replicas pointing to unknown Na'vi are dropped; replicas of
disconnected Na'vi are removed (this is what triggers `replicaLost` for self-healing).
