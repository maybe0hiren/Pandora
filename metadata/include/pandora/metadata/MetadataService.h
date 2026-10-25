#pragma once
/**
 * @file MetadataService.h
 * @brief Public API of the Metadata & Cloud Management module (Member 3).
 *
 * Every Na'vi runs one MetadataService holding 100% of the metadata in a local SQLite database.
 * The service never touches sockets, files or the UI. It talks to the rest of the system in two ways only:
 *
 *  - **Input**:  handleMessage(), handlePeerLost() and the explicit API calls below.
 *  - **Output**: Qt signals. `outgoingMessage()` carries every event that must be sent to other Na'vi; the
 *    integration glue forwards it to Networking (`broadcastMessage()` when Message::isBroadcast(),
 *    `sendMessage()` otherwise). Signals such as `replicaLost()` tell Storage what to heal.
 *
 * Threading: all public methods are guarded by a recursive mutex, but the object is a QObject and its
 * signals are emitted in the thread that called into it. Use it from one thread (the GUI/main thread
 * is the intended one) or connect with Qt::QueuedConnection when crossing threads.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include <pandora/shared/Constants.h>
#include <pandora/shared/FileMetadata.h>
#include <pandora/shared/Message.h>
#include <pandora/shared/Node.h>
#include <pandora/shared/Pandora.h>

namespace pandora::metadata {

class MetadataStore;  // private implementation detail (SQLite access); not part of the public API

/// Outcome of an API call. `ok == false` carries a human readable `error`.
struct Result {
    bool ok = true;
    QString error;

    explicit operator bool() const { return ok; }
    static Result success() { return {}; }
    static Result failure(const QString& why) { return {false, why}; }
};

/// One line of the audit log.
struct LogEntry {
    qint64 seq = 0;
    qint64 timestampMs = 0;
    QString level;     ///< "INFO", "WARN" or "ERROR"
    QString category;  ///< lifecycle, membership, file, sync, election, shutdown, protocol
    QString message;
};

/// Output format of MetadataService::generateLogs().
enum class LogFormat { Text, Json };

/// Construction parameters.
struct MetadataConfig {
    /// SQLite database location. ":memory:" (default) leaves nothing on disk. A file path is erased on wipe.
    QString databasePath = QStringLiteral(":memory:");
    /// Length of the shutdown countdown. Production value is 120 s; tests use a small value.
    int shutdownGraceSeconds = pandora::shared::constants::kShutdownGraceSeconds;
};

/**
 * @class MetadataService
 * @brief Metadata replica, Na'vi registry, Guardian election, cloud lifecycle and logging for one node.
 */
class MetadataService : public QObject {
    Q_OBJECT
public:
    /**
     * @param self    The local node. `self.id` comes from Security::generateNodeID(). Must be valid.
     * @param config  Database location and shutdown grace period.
     */
    explicit MetadataService(const pandora::shared::Node& self, MetadataConfig config = {},
                             QObject* parent = nullptr);
    ~MetadataService() override;

    MetadataService(const MetadataService&) = delete;
    MetadataService& operator=(const MetadataService&) = delete;

    // ------------------------------------------------------------------ lifecycle

    /// Opens (creates) the local database and its schema. Must be called before anything else.
    /// Calling it again after a wipe starts from a clean database.
    Result open();

    /**
     * Creates a brand-new Pandora with the local node as Guardian and first Na'vi.
     * @param pandora  id, keyHash (from Security) and maxNavi must be set; guardianId/state/epoch are overwritten.
     * Emits outgoingMessage(CREATE_PANDORA).
     */
    Result createPandora(pandora::shared::Pandora pandora);

    /**
     * Admits a joining node. **Guardian only**: admission is serialised on one node so the Na'vi limit can never be
     * exceeded by two simultaneous joins. A node that receives a JOIN_REQUEST while not being the Guardian must forward
     * it to guardianId(). Call this AFTER Security validated the Pandora Key (see joinRequested()). Checks capacity, registers the node, emits NAVI_JOINED (broadcast) and returns the
     * JOIN_ACCEPTED message (addressed to the new node, holds a full metadata snapshot) in @p reply.
     */
    Result admitNavi(pandora::shared::Node candidate, pandora::shared::Message* reply);

    /// Gracefully leaves: broadcasts NAVI_LEFT and wipes all local state. No final logs are produced.
    Result leavePandora();

    // ------------------------------------------------------------------ file metadata (local origin)
    //
    // These calls are made when THIS node performed an operation (normally relayed from a Storage event).
    // They apply the change locally, then emit the matching event through outgoingMessage().

    /// Registers a new file (FILE_UPLOAD). Fails if the name already exists. Fills version=1, time, writer if unset.
    Result addMetadata(pandora::shared::FileMetadata file);

    /// Records an edit (FILE_UPDATE). Version is set to previous+1, writer and time to local node / now.
    /// Size, checksum and (if non-empty) replica list are taken from @p file.
    Result updateMetadata(const pandora::shared::FileMetadata& file);

    /// Deletes a file immediately (FILE_DELETE). Final: the id is tombstoned.
    Result removeMetadata(const QString& fileId);

    /// Adds/removes replica locations of a file (METADATA_SYNC kind "replica_update"); used by replication/self-healing.
    Result updateReplicas(const QString& fileId, const QStringList& addNodeIds, const QStringList& removeNodeIds);

    // ------------------------------------------------------------------ synchronisation

    /// Asks the other Na'vi for a snapshot (METADATA_SYNC "snapshot_request"). Call after joining and when a
    /// consistency check is wanted. Whoever receives it answers with a snapshot addressed to this node.
    void syncMetadata();

    /// Full copy of the metadata (Pandora, Na'vi, files, replicas, tombstones) as JSON.
    QJsonObject exportSnapshot() const;

    /// Merges a snapshot into the local database using the same conflict rules as live events.
    Result importSnapshot(const QJsonObject& snapshot);

    // ------------------------------------------------------------------ Guardian & shutdown

    /**
     * Deterministic Guardian election. Only valid when the current Guardian is no longer connected.
     * "Random" but identical on every node: the winner is chosen by hashing (pandoraId, newEpoch, oldGuardianId)
     * over the sorted list of connected Na'vi, so all nodes pick the same Guardian without coordination.
     * Normally invoked automatically when the Guardian leaves; exposed for explicit use.
     * @param[out] newGuardianId  receives the elected id (optional).
     */
    Result electGuardian(QString* newGuardianId = nullptr);

    /// Guardian only: broadcasts SHUTDOWN_WARNING and starts the countdown. When it reaches zero PANDORA_DEACTIVATE is
    /// broadcast and every node wipes everything.
    Result shutdownPandora();

    // ------------------------------------------------------------------ logs

    /// Renders THIS node's audit log.
    QString generateLogs(LogFormat format = LogFormat::Text) const;

    /// Guardian only: asks every Na'vi for its log (METADATA_SYNC "logs_request"). Replies are kept in memory and
    /// announced with logsReceived(). Also done automatically when the shutdown countdown starts.
    Result requestLogs();

    /// This node's log merged with all logs collected so far by requestLogs(), ordered by time, each line tagged with the
    /// Na'vi it came from. This is what the Guardian sees and what finalLogsReady() delivers.
    QString generateClusterLogs(LogFormat format = LogFormat::Text) const;
    QList<LogEntry> logEntries() const;

    // ------------------------------------------------------------------ read-only queries

    bool isOpen() const;
    bool hasPandora() const;
    pandora::shared::Node self() const;
    pandora::shared::Pandora pandora() const;
    bool isGuardian() const;
    QString guardianId() const;
    QList<pandora::shared::Node> listNavis(bool onlyConnected = true) const;  ///< storageUsedBytes is filled in
    bool getNavi(const QString& naviId, pandora::shared::Node* out) const;
    QList<pandora::shared::FileMetadata> listFiles() const;
    bool getFile(const QString& fileId, pandora::shared::FileMetadata* out) const;
    bool getFileByName(const QString& name, pandora::shared::FileMetadata* out) const;
    /// Replica holders of a file that are currently connected.
    QStringList availableReplicas(const QString& fileId) const;
    /// Bytes of Pandora-managed replicas on a node (basis for "least-loaded" selection).
    qint64 storageUsage(const QString& naviId) const;
    QMap<QString, qint64> storageUsageByNavi() const;  ///< connected Na'vi only

public slots:
    /// Feed every received Message here. Types not relevant to metadata (FILE_REQUEST/RESPONSE) are ignored.
    void handleMessage(const pandora::shared::Message& message);

    /// Feed Networking's "peer disconnected" detection here. Every node applies the departure locally, so no
    /// message from the dead node is needed.
    void handlePeerLost(const QString& naviId);

signals:
    /// An event for other Na'vi. Forward to Networking (broadcast if isBroadcast(), else sendMessage()).
    void outgoingMessage(const pandora::shared::Message& message);

    /// A node asked to join. Validate the key (Security), then call admitNavi().
    void joinRequested(const pandora::shared::Node& candidate, const QString& senderId);

    void naviJoined(const pandora::shared::Node& navi);
    void naviLeft(const QString& naviId);

    void fileAdded(const pandora::shared::FileMetadata& file);
    void fileUpdated(const pandora::shared::FileMetadata& file);
    void fileRemoved(const QString& fileId);          ///< also emitted when a duplicate-name conflict removes a loser
    void replicasChanged(const QString& fileId);

    /// A replica holder left; Storage should re-replicate if the file is now under-replicated (self-healing trigger).
    void replicaLost(const QString& fileId, const QString& naviId);

    void guardianChanged(const QString& oldGuardianId, const QString& newGuardianId);
    void metadataSynced();
    void logsReceived(const QString& naviId);     ///< a Na'vi's log arrived (Guardian only)

    void shutdownWarning(int secondsRemaining);   ///< UI: show Shutdown Warning screen, enable Download / Download All
    void shutdownCountdown(int secondsRemaining); ///< once per second
    /// Emitted only on the Guardian, just before the wipe. Receiver should save/show it; the service then deletes its copy.
    void finalLogsReady(const QString& logs);
    /// Everything is wiped. UI returns to the initial screen; Storage/Security must erase their data too.
    void pandoraDeactivated();

    void errorOccurred(const QString& description);

private slots:
    void onCountdownTick();

private:
    struct Impl;
    std::unique_ptr<Impl> d;

    enum class Applied { Inserted, Updated, Ignored };

    // helpers (all expect d->mutex to be held)
    void log(const QString& level, const QString& category, const QString& message);
    void send(pandora::shared::MessageType type, const QJsonObject& payload, const QString& recipientId = {});
    Result requireActive(pandora::shared::Pandora* out = nullptr) const;
    QStringList onlyConnected(const QStringList& naviIds) const;
    Applied applyRemoteFile(const pandora::shared::FileMetadata& file);
    void applyRemoteDelete(const QString& fileId);
    void applyReplicaUpdate(const QString& fileId, const QStringList& add, const QStringList& remove);
    void applyNaviLeft(const QString& naviId);
    Result electAfterGuardianLoss(QString* newGuardianId);
    void applyGuardianChanged(const pandora::shared::Message& m);
    void beginCountdown(int seconds);
    void wipe(bool produceFinalLogs);
    QString renderLogs(LogFormat format, bool cluster) const;
    void requestLogsInternal();
};

}  // namespace pandora::metadata
