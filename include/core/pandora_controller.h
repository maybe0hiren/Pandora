#pragma once

#include "core/event_bus.h"
#include "metadata/metadata_store.h"
#include "network/peer_node.h"
#include "security/session_security.h"
#include "storage/storage_manager.h"
#include <QHash>
#include <QObject>
#include <QTimer>

namespace pandora {

class PandoraController final : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 kTransferChunkBytes = 256 * 1024; // 256 KiB network chunks

    explicit PandoraController(QObject* parent = nullptr);
    ~PandoraController() override;

    MetadataStore* metadata() { return &metadata_; }
    StorageManager* storage() { return &storage_; }
    PeerNode* network() { return &network_; }

    QUuid localNodeId() const { return localNodeId_; }
    QString localNodeIdString() const { return localNodeId_.toString(QUuid::WithoutBraces); }
    QString localUserName() const { return localUserName_; }

    void setPreferredPorts(quint16 tcpPort, quint16 discoveryPort = 43171);
    quint16 tcpPort() const { return network_.isRunning() ? network_.port() : tcpPort_; }
    quint16 discoveryPort() const { return discoveryPort_; }

    void setReplicationFactor(int factor);
    int replicationFactor() const { return replicationFactor_; }

    void setQuotaBytes(qint64 bytes);
    qint64 quotaBytes() const { return storage_.quotaBytes(); }

    bool startLanDiscovery();
    QList<DiscoveredSession> discoveredSessions() const;

    bool createPandora(const QString& pandoraName, const QString& userName, const QString& key, QString* error = nullptr);
    bool joinPandora(const QString& userName, const QString& key, const QString& pandoraId, QString* error = nullptr);
    bool joinPandoraDirect(const QString& userName, const QString& key, const QString& hostAddress, quint16 hostPort, const QString& pandoraId = {}, QString* error = nullptr);

    bool uploadFile(const QString& path, QString* error = nullptr, const QString& relativeNameOverride = {}, const QString& folderGroup = {});
    bool uploadFolder(const QString& folderPath, int* uploadedCount = nullptr, QString* error = nullptr);

    bool downloadFile(const QString& fileId, const QString& destination, QString* error = nullptr);
    bool downloadAllFiles(const QString& destinationDirectory, int* exportedOrQueuedCount = nullptr, QString* error = nullptr);
    bool deleteFile(const QString& fileId, QString* error = nullptr);

    bool initiateGuardianShutdown(int countdownSeconds = 120, QString* error = nullptr);
    bool cancelGuardianShutdown(QString* error = nullptr);
    void deactivatePandora(bool broadcastIfGuardian = true);

    bool isActive() const { return active_; }
    bool isGuardian() const;
    bool isShuttingDown() const { return shutdownRemainingSeconds_ > 0; }
    int shutdownRemainingSeconds() const { return shutdownRemainingSeconds_; }

    void triggerReplicaAuditAndRepair();
    void runMaintenanceNow() { onPeriodicMaintenance(); }

signals:
    void statusChanged(const QString& status);
    void logLine(const QString& line);
    void discoveredSessionsChanged();
    void joinSucceeded(const QString& pandoraName);
    void joinFailed(const QString& reason);
    void transferProgressUpdated(const pandora::TransferProgress& progress);
    void remoteDownloadCompleted(const QString& fileId, const QString& destinationPath);
    void shutdownCountdownUpdated(int secondsRemaining);

private slots:
    void onMessage(const QJsonObject& message, const QString& peerNodeId, QTcpSocket* socket, const QString& peerAddress);
    void onSessionDiscovered(const pandora::DiscoveredSession& session);
    void onPeerConnected(const QString& nodeId, const QString& address, quint16 port);
    void onPeerDisconnected(const QString& nodeId);
    void onEvent(const pandora::PandoraEvent& event);
    void onPeriodicMaintenance();
    void onShutdownTick();

private:
    struct PendingJoinState {
        bool active{false};
        QString userName;
        QString key;
        QString targetPandoraId;
        QString hostAddress;
        quint16 hostPort{0};
        QTcpSocket* socket{nullptr};
    };

    struct PendingChallenge {
        QByteArray nonce;
        QString clientNodeId;
        QString clientName;
        quint16 clientListenPort{0};
        qint64 clientQuotaBytes{0};
        qint64 clientAvailableBytes{0};
        QDateTime issuedAtUtc;
    };

    struct OutboundPushState {
        QString transferId;
        QString fileId;
        QString fileName;
        QString targetNodeId;
        qint64 fileSize{0};
        QString sha256;
        qint64 nextOffset{0};
        QString direction{"Upload"};
    };

    struct InboundPullState {
        QString transferId;
        QString fileId;
        QString fileName;
        QString sourceNodeId;
        QString exportDestination; // Non-empty when user requested Download
        qint64 fileSize{0};
        QString sha256;
        qint64 nextOffset{0};
    };

    void publish(EventType type, const QJsonObject& payload);
    QStringList chooseReplicas(int count, qint64 fileSize, const QStringList& excludeNodeIds) const;
    void startReplicationForFile(const FileInfo& f);
    void pushReplicaToNode(const FileInfo& f, const QString& targetNodeId, const QString& directionLabel = "Replication");
    void sendNextPushChunk(const QString& transferKey);
    void requestPullFromNode(const FileInfo& f, const QString& sourceNodeId, const QString& exportDest);
    void connectToKnownPeers();
    void checkGuardianHealthAndElect();

    MetadataStore metadata_;
    StorageManager storage_;
    PeerNode network_;
    EventBus eventBus_;

    QUuid localNodeId_{QUuid::createUuid()};
    QString localUserName_;
    QByteArray salt_;
    QByteArray masterKey_;
    QByteArray keyVerifier_;
    QByteArray transportKey_;

    QHash<QString, DiscoveredSession> discoveredSessions_;
    QHash<QTcpSocket*, PendingChallenge> pendingChallenges_;
    PendingJoinState pendingJoin_;
    QHash<QString, OutboundPushState> activePushes_; // key: fileId + ":" + targetNodeId
    QHash<QString, InboundPullState> activePulls_;   // key: fileId

    QTimer maintenanceTimer_;
    QTimer shutdownTickTimer_;
    int shutdownRemainingSeconds_{0};
    int replicationFactor_{3};
    quint16 tcpPort_{43170};
    quint16 discoveryPort_{43171};
    quint64 eventSeq_{0};
    bool active_{false};
};

} // namespace pandora
Q_DECLARE_METATYPE(pandora::TransferProgress)
Q_DECLARE_METATYPE(pandora::DiscoveredSession)
