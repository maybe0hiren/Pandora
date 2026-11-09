# Pandora Production Architecture

## Module Responsibilities

- **`MainWindow` (`include/ui/main_window.h`, `src/ui/main_window.cpp`)**:
  - Tabbed desktop interface for **Create Pandora**, **Join Pandora (LAN)**, and **Settings & Quotas**.
  - Real-time distributed file catalog showing file sizes, replica counts (`local`/`remote`), versions, and SHA-256 prefixes.
  - Real-time connected Na'vi peer table with role badges (`Guardian` vs. `Na'vi Peer`), network endpoints, storage quota usage, and free disk capacity.
  - **Upload File**, **Upload Folder**, **Download Selected**, **Download All**, **Delete Selected**, and **Guardian 2-Min Shutdown Countdown** banner.
- **`PandoraController` (`include/core/pandora_controller.h`, `src/core/pandora_controller.cpp`)**:
  - Orchestrates session lifecycle, challenge-response authentication, full peer mesh formation, chunked file replication, remote file pulls, periodic replica health audits, automatic self-healing, deterministic Guardian failover, and coordinated shutdown countdowns.
- **`PeerNode` (`include/network/peer_node.h`, `src/network/peer_node.cpp`)**:
  - Manages UDP multi-interface LAN broadcasts and persistent bidirectional TCP connections (`connections_` and `nodeSockets_`).
  - Enforces 8 MiB maximum frame size, heartbeat timeouts, and transparent AEAD encryption/decryption with replay protection.
- **`MetadataStore` (`include/metadata/metadata_store.h`, `src/metadata/metadata_store.cpp`)**:
  - Thread-safe (`QMutex`) store backed by a per-session SQLite WAL database (`metadata.sqlite`).
  - Implements version-vector causality comparison, deterministic conflict preservation (auto-renaming concurrent same-name uploads), tombstone protection against deleted-file resurrection, event deduplication (`seenEvents_`), and deterministic Guardian election (`electDeterministicGuardian`).
- **`StorageManager` (`include/storage/storage_manager.h`, `src/storage/storage_manager.cpp`)**:
  - Manages the per-session temporary directory (`<Temp>/pandora-<UUID>`) with `.session.pid` process lockfiles for automatic crash-orphan cleanup on startup.
  - Performs atomic `.part` -> `.blob` writes, single-pass SHA-256 streaming, 256 KiB chunked network I/O, quota and free-disk enforcement, path-traversal sanitization, and zero-fill file erasure on deletion/deactivation.
- **`SessionSecurity` (`include/security/session_security.h`, `src/security/session_security.cpp`)**:
  - Provides 60,000-round PBKDF2-HMAC-SHA256 key derivation, HMAC challenge-response proofs, constant-time equality checks, and Encrypt-then-MAC AEAD JSON sealing.
