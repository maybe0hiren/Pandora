#include "core/pandora_controller.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <algorithm>

namespace pandora {

PandoraController::PandoraController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<PandoraEvent>();
    qRegisterMetaType<TransferProgress>();
    qRegisterMetaType<DiscoveredSession>();

    network_.setLocalNodeId(localNodeIdString());

    connect(&network_, &PeerNode::messageReceived, this, &PandoraController::onMessage);
    connect(&network_, &PeerNode::sessionDiscovered, this, &PandoraController::onSessionDiscovered);
    connect(&network_, &PeerNode::peerConnected, this, &PandoraController::onPeerConnected);
    connect(&network_, &PeerNode::peerDisconnected, this, &PandoraController::onPeerDisconnected);
    connect(&eventBus_, &EventBus::eventPublished, this, &PandoraController::onEvent);

    connect(&storage_, &StorageManager::storageChanged, this, [this](qint64 bytes) {
        if (!active_) return;
        const QString myId = localNodeIdString();
        NodeInfo me = metadata_.node(myId);
        if (!me.id.isNull()) {
            me.managedBytes = bytes;
            me.availableDiskBytes = storage_.availableDiskBytes();
            me.quotaBytes = storage_.quotaBytes();
            metadata_.addNode(me);
            publish(EventType::NodeUpdated, {{"node", me.toJson()}});
        }
    });

    maintenanceTimer_.setInterval(2500);
    connect(&maintenanceTimer_, &QTimer::timeout, this, &PandoraController::onPeriodicMaintenance);

    shutdownTickTimer_.setInterval(1000);
    connect(&shutdownTickTimer_, &QTimer::timeout, this, &PandoraController::onShutdownTick);

    StorageManager::cleanupOrphanedSessions();
    network_.startDiscoveryOnly(discoveryPort_);
}

PandoraController::~PandoraController() {
    if (active_) {
        deactivatePandora(false);
    }
}

void PandoraController::setPreferredPorts(quint16 tcpPort, quint16 discoveryPort) {
    tcpPort_ = tcpPort;
    discoveryPort_ = discoveryPort;
    if (!active_) {
        network_.startDiscoveryOnly(discoveryPort_);
    }
}

void PandoraController::setReplicationFactor(int factor) {
    replicationFactor_ = qBound(1, factor, 10);
    if (active_) {
        PandoraSession s = metadata_.session();
        s.replicationFactor = replicationFactor_;
        metadata_.setSession(s);
        triggerReplicaAuditAndRepair();
    }
}

void PandoraController::setQuotaBytes(qint64 bytes) {
    storage_.setQuotaBytes(bytes);
    if (active_) {
        NodeInfo me = metadata_.node(localNodeIdString());
        if (!me.id.isNull()) {
            me.quotaBytes = storage_.quotaBytes();
            me.availableDiskBytes = storage_.availableDiskBytes();
            metadata_.addNode(me);
            publish(EventType::NodeUpdated, {{"node", me.toJson()}});
        }
    }
}

bool PandoraController::startLanDiscovery() {
    return network_.startDiscoveryOnly(discoveryPort_);
}

QList<DiscoveredSession> PandoraController::discoveredSessions() const {
    QList<DiscoveredSession> list = discoveredSessions_.values();
    std::sort(list.begin(), list.end(), [](const DiscoveredSession& a, const DiscoveredSession& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return list;
}

bool PandoraController::isGuardian() const {
    return active_ && metadata_.guardian() == localNodeIdString();
}

bool PandoraController::createPandora(const QString& name, const QString& user, const QString& key, QString* error) {
    if (active_) {
        if (error) *error = "Pandora is already active.";
        return false;
    }
    if (name.trimmed().isEmpty() || user.trimmed().isEmpty() || key.size() < 6) {
        if (error) *error = "Enter a Pandora name, your Na'vi name, and a Pandora Key of at least 6 characters.";
        return false;
    }
    if (!storage_.initialize()) {
        if (error) *error = "Could not initialize temporary storage.";
        return false;
    }
    metadata_.initializePersistence(storage_.rootPath() + "/metadata.sqlite");

    if (!network_.start(tcpPort_, discoveryPort_)) {
        storage_.destroySessionData();
        metadata_.reset();
        if (error) *error = "Could not start LAN networking. Check ports or firewall.";
        return false;
    }

    localUserName_ = user.trimmed();
    salt_ = SessionSecurity::createSalt();
    masterKey_ = SessionSecurity::deriveMasterKey(key, salt_);
    keyVerifier_ = SessionSecurity::deriveVerifier(key, salt_);

    PandoraSession s;
    s.id = QUuid::createUuid();
    s.name = name.trimmed();
    s.guardianNodeId = localNodeIdString();
    s.salt = salt_;
    s.keyVerifier = keyVerifier_;
    s.replicationFactor = replicationFactor_;
    s.term = 1;
    s.active = true;

    transportKey_ = SessionSecurity::deriveTransportKey(masterKey_, s.id.toString(QUuid::WithoutBraces));
    network_.setTransportKey(transportKey_);

    metadata_.setSession(s);
    metadata_.setGuardian(localNodeIdString(), 1);

    NodeInfo self;
    self.id = localNodeId_;
    self.name = localUserName_;
    self.address = "127.0.0.1";
    self.port = network_.port();
    self.connected = true;
    self.managedBytes = 0;
    self.quotaBytes = storage_.quotaBytes();
    self.availableDiskBytes = storage_.availableDiskBytes();
    self.joinedAt = QDateTime::currentDateTimeUtc();
    self.lastSeenUtc = self.joinedAt;
    self.electionTerm = 1;
    metadata_.addNode(self);

    active_ = true;
    shutdownRemainingSeconds_ = 0;
    maintenanceTimer_.start();

    publish(EventType::SessionCreated, {{"pandoraId", s.id.toString(QUuid::WithoutBraces)}, {"name", s.name}});
    network_.announce({
        {"pandoraId", s.id.toString(QUuid::WithoutBraces)},
        {"port", static_cast<int>(network_.port())},
        {"name", s.name},
        {"hostName", localUserName_},
        {"nodeCount", 1}
    });

    emit statusChanged(QString("Pandora Active — %1 (Guardian)").arg(s.name));
    emit logLine(QString("Created Pandora '%1' (ID: %2) on TCP port %3. Local node is Guardian.")
                 .arg(s.name, s.id.toString(QUuid::WithoutBraces)).arg(network_.port()));
    return true;
}

bool PandoraController::joinPandora(const QString& user, const QString& key, const QString& pandoraId, QString* error) {
    if (active_) {
        if (error) *error = "Pandora is already active.";
        return false;
    }
    if (user.trimmed().isEmpty() || key.size() < 6) {
        if (error) *error = "Enter your Na'vi name and a Pandora Key (6+ characters).";
        return false;
    }

    DiscoveredSession target;
    if (!pandoraId.trimmed().isEmpty() && discoveredSessions_.contains(pandoraId.trimmed())) {
        target = discoveredSessions_.value(pandoraId.trimmed());
    } else if (pandoraId.trimmed().isEmpty() && discoveredSessions_.size() == 1) {
        target = discoveredSessions_.values().first();
    } else {
        for (const auto& ds : discoveredSessions_) {
            if (ds.pandoraId.compare(pandoraId.trimmed(), Qt::CaseInsensitive) == 0 ||
                ds.name.compare(pandoraId.trimmed(), Qt::CaseInsensitive) == 0) {
                target = ds;
                break;
            }
        }
    }

    if (target.address.isEmpty() || target.port == 0) {
        if (error) *error = "Could not find the requested Pandora session on the LAN. Select a discovered session or join by host IP:Port.";
        return false;
    }
    return joinPandoraDirect(user, key, target.address, target.port, target.pandoraId, error);
}

bool PandoraController::joinPandoraDirect(const QString& userName, const QString& key, const QString& hostAddress, quint16 hostPort, const QString& pandoraId, QString* error) {
    if (active_) {
        if (error) *error = "A Pandora session is already active.";
        return false;
    }
    if (userName.trimmed().isEmpty() || key.size() < 6) {
        if (error) *error = "Enter your Na'vi name and a valid Pandora Key (at least 6 characters).";
        return false;
    }
    if (hostAddress.trimmed().isEmpty() || hostPort == 0) {
        if (error) *error = "Host address and port are required.";
        return false;
    }

    if (!storage_.initialize()) {
        if (error) *error = "Could not initialize local temporary storage.";
        return false;
    }
    metadata_.initializePersistence(storage_.rootPath() + "/metadata.sqlite");

    if (!network_.start(tcpPort_, discoveryPort_)) {
        storage_.destroySessionData();
        metadata_.reset();
        if (error) *error = "Could not bind local networking port.";
        return false;
    }

    localUserName_ = userName.trimmed();
    pendingJoin_.active = true;
    pendingJoin_.userName = localUserName_;
    pendingJoin_.key = key;
    pendingJoin_.targetPandoraId = pandoraId.trimmed();
    pendingJoin_.hostAddress = hostAddress.trimmed();
    pendingJoin_.hostPort = hostPort;

    emit statusChanged("Authenticating with Pandora host...");
    emit logLine(QString("Connecting to Pandora host at %1:%2...").arg(hostAddress).arg(hostPort));

    QJsonObject joinReq{
        {"kind", "join_request"},
        {"pandoraId", pendingJoin_.targetPandoraId},
        {"nodeId", localNodeIdString()},
        {"name", localUserName_},
        {"listenPort", static_cast<int>(network_.port())},
        {"quotaBytes", QString::number(storage_.quotaBytes())},
        {"availableDiskBytes", QString::number(storage_.availableDiskBytes())}
    };
    network_.sendToAddress(pendingJoin_.hostAddress, pendingJoin_.hostPort, joinReq);
    return true;
}

bool PandoraController::uploadFile(const QString& path, QString* error, const QString& relativeNameOverride, const QString& folderGroup) {
    if (!active_) {
        if (error) *error = "Pandora is not active.";
        return false;
    }
    if (isShuttingDown()) {
        if (error) *error = "Pandora is shutting down; new uploads are disabled.";
        return false;
    }

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        if (error) *error = "Choose a valid file to upload.";
        return false;
    }

    const QString rawName = relativeNameOverride.isEmpty() ? fi.fileName() : relativeNameOverride;
    const QString safeName = StorageManager::sanitizeRelativePath(rawName);
    if (safeName.isEmpty()) {
        if (error) *error = "Invalid or unsafe file name.";
        return false;
    }

    for (const auto& existing : metadata_.files()) {
        if (existing.name.compare(safeName, Qt::CaseInsensitive) == 0) {
            if (error) *error = QString("Duplicate file name '%1' is not allowed.").arg(safeName);
            return false;
        }
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString sha256;
    qint64 storedSize = 0;
    if (!storage_.storeFile(path, id, &sha256, &storedSize, error)) {
        return false;
    }

    FileInfo f;
    f.id = QUuid(id);
    f.name = safeName;
    f.folderGroup = folderGroup;
    f.size = storedSize;
    f.sha256 = sha256;
    f.version = 1;
    f.modifiedBy = localNodeId_;
    f.modifiedAt = QDateTime::currentDateTimeUtc();
    f.replicas = {localNodeIdString()};
    f.versionVector[localNodeIdString()] = 1;

    QString why;
    if (!metadata_.addFile(f, &why)) {
        storage_.removeFile(id);
        if (error) *error = why;
        return false;
    }

    publish(EventType::FileAdded, {{"file", f.toJson()}});
    emit logLine(QString("Uploaded '%1' (%2 bytes, SHA-256: %3...).")
                 .arg(f.name).arg(f.size).arg(f.sha256.left(12)));

    startReplicationForFile(f);
    return true;
}

bool PandoraController::uploadFolder(const QString& folderPath, int* uploadedCount, QString* error) {
    if (!active_) {
        if (error) *error = "Pandora is not active.";
        return false;
    }
    QDir rootDir(folderPath);
    if (!rootDir.exists()) {
        if (error) *error = "Selected folder does not exist.";
        return false;
    }

    const QString folderName = StorageManager::sanitizeRelativePath(rootDir.dirName());
    int count = 0;
    QDirIterator it(folderPath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString relInside = rootDir.relativeFilePath(filePath);
        const QString combinedRel = StorageManager::sanitizeRelativePath(
            folderName.isEmpty() ? relInside : (folderName + "/" + relInside));
        QString fileErr;
        if (!uploadFile(filePath, &fileErr, combinedRel, folderName)) {
            if (error) *error = QString("Failed on '%1': %2").arg(combinedRel, fileErr);
            if (uploadedCount) *uploadedCount = count;
            return false;
        }
        ++count;
    }
    if (uploadedCount) *uploadedCount = count;
    emit logLine(QString("Uploaded folder '%1' (%2 files).").arg(folderName).arg(count));
    return true;
}

bool PandoraController::downloadFile(const QString& id, const QString& dest, QString* error) {
    bool found = false;
    const FileInfo f = metadata_.file(id, &found);
    if (!found || f.tombstone) {
        if (error) *error = "File not found in Pandora.";
        return false;
    }

    // 1. If we already have a verified local replica, export immediately
    if (storage_.hasReplica(id) && storage_.verifyReplica(id, f.sha256, f.size)) {
        if (!storage_.exportFile(id, dest, error)) return false;
        emit logLine(QString("Exported '%1' from local replica to %2").arg(f.name, dest));
        emit remoteDownloadCompleted(id, dest);
        return true;
    }

    // 2. Otherwise locate an online peer holding a replica and pull it
    QStringList candidatePeers;
    for (const QString& peerId : f.replicas) {
        if (peerId != localNodeIdString() && network_.isNodeConnected(peerId)) {
            candidatePeers.append(peerId);
        }
    }
    // Fallback: ask any connected peer if replica list is stale
    if (candidatePeers.isEmpty()) {
        candidatePeers = network_.connectedNodeIds();
    }

    if (candidatePeers.isEmpty()) {
        if (error) *error = "No online peers holding a replica of this file are currently reachable.";
        return false;
    }

    const QString sourceNode = candidatePeers.first();
    requestPullFromNode(f, sourceNode, dest);
    emit logLine(QString("Requesting remote download of '%1' from peer %2...").arg(f.name, sourceNode.left(8)));
    return true;
}

bool PandoraController::downloadAllFiles(const QString& destinationDirectory, int* exportedOrQueuedCount, QString* error) {
    if (!active_) {
        if (error) *error = "Pandora is not active.";
        return false;
    }
    QDir outDir(destinationDirectory);
    if (!outDir.exists() && !QDir().mkpath(destinationDirectory)) {
        if (error) *error = "Could not create destination directory.";
        return false;
    }

    int count = 0;
    for (const FileInfo& f : metadata_.files(false)) {
        const QString safeRel = StorageManager::sanitizeRelativePath(f.name);
        if (safeRel.isEmpty()) continue;
        const QString targetPath = outDir.filePath(safeRel);
        QString fileErr;
        if (downloadFile(f.id.toString(QUuid::WithoutBraces), targetPath, &fileErr)) {
            ++count;
        }
    }
    if (exportedOrQueuedCount) *exportedOrQueuedCount = count;
    emit logLine(QString("Download-All initiated for %1 files into %2.").arg(count).arg(destinationDirectory));
    return true;
}

bool PandoraController::deleteFile(const QString& id, QString* error) {
    if (!active_) {
        if (error) *error = "Pandora is not active.";
        return false;
    }
    bool found = false;
    const FileInfo existing = metadata_.file(id, &found);
    if (!found || existing.tombstone) {
        if (error) *error = "File not found.";
        return false;
    }

    if (!metadata_.deleteFile(id, localNodeId_, error)) return false;
    storage_.removeFile(id, true);

    const FileInfo tombstoned = metadata_.file(id);
    publish(EventType::FileDeleted, {{"fileId", id}, {"file", tombstoned.toJson()}});
    emit logLine(QString("Deleted '%1' and broadcast tombstone to all replicas.").arg(existing.name));
    return true;
}

bool PandoraController::initiateGuardianShutdown(int countdownSeconds, QString* error) {
    if (!active_) {
        if (error) *error = "Pandora is not active.";
        return false;
    }
    if (!isGuardian()) {
        if (error) *error = "Only the current Guardian can initiate a session-wide shutdown countdown.";
        return false;
    }

    shutdownRemainingSeconds_ = qMax(1, countdownSeconds);
    PandoraSession s = metadata_.session();
    s.shuttingDown = true;
    s.shutdownDeadlineUtc = QDateTime::currentDateTimeUtc().addSecs(shutdownRemainingSeconds_);
    metadata_.setSession(s);

    shutdownTickTimer_.start();
    publish(EventType::ShutdownWarning, {
        {"secondsRemaining", shutdownRemainingSeconds_},
        {"deadlineUtc", s.shutdownDeadlineUtc.toUTC().toString(Qt::ISODateWithMs)},
        {"guardianNodeId", localNodeIdString()}
    });
    emit shutdownCountdownUpdated(shutdownRemainingSeconds_);
    emit statusChanged(QString("Shutdown Countdown: %1s remaining").arg(shutdownRemainingSeconds_));
    emit logLine(QString("Guardian initiated %1-second shutdown countdown. Export needed files now.").arg(shutdownRemainingSeconds_));
    return true;
}

bool PandoraController::cancelGuardianShutdown(QString* error) {
    if (!active_ || !isGuardian()) {
        if (error) *error = "Only the Guardian can cancel shutdown.";
        return false;
    }
    shutdownTickTimer_.stop();
    shutdownRemainingSeconds_ = 0;
    PandoraSession s = metadata_.session();
    s.shuttingDown = false;
    s.shutdownDeadlineUtc = {};
    metadata_.setSession(s);

    publish(EventType::ShutdownCancelled, {{"guardianNodeId", localNodeIdString()}});
    emit shutdownCountdownUpdated(0);
    emit statusChanged(QString("Pandora Active — %1 (Guardian)").arg(s.name));
    emit logLine("Guardian cancelled the shutdown countdown.");
    return true;
}

void PandoraController::deactivatePandora(bool broadcastIfGuardian) {
    if (!active_ && !pendingJoin_.active) return;

    maintenanceTimer_.stop();
    shutdownTickTimer_.stop();
    shutdownRemainingSeconds_ = 0;

    if (active_) {
        if (broadcastIfGuardian && isGuardian()) {
            publish(EventType::SessionDeactivated, {{"guardianNodeId", localNodeIdString()}});
        } else {
            publish(EventType::NodeLeft, {{"nodeId", localNodeIdString()}});
        }
    }

    activePushes_.clear();
    activePulls_.clear();
    pendingChallenges_.clear();
    pendingJoin_ = {};

    network_.stop();
    metadata_.reset();
    storage_.destroySessionData(true);

    active_ = false;
    salt_.clear();
    masterKey_.clear();
    keyVerifier_.clear();
    transportKey_.clear();

    network_.startDiscoveryOnly(discoveryPort_);
    emit shutdownCountdownUpdated(0);
    emit statusChanged("Pandora Inactive");
    emit logLine("Pandora deactivated and temporary replica storage securely wiped.");
}

void PandoraController::publish(EventType type, const QJsonObject& payload) {
    PandoraEvent e;
    e.type = type;
    e.originNodeId = localNodeIdString();
    e.sequence = ++eventSeq_;
    e.term = metadata_.term();
    e.payload = payload;

    metadata_.recordEvent(e);
    eventBus_.publish(e);
    network_.broadcastEvent(e);
}

QStringList PandoraController::chooseReplicas(int count, qint64 fileSize, const QStringList& excludeNodeIds) const {
    auto ns = metadata_.nodes(true);
    std::sort(ns.begin(), ns.end(), [](const NodeInfo& a, const NodeInfo& b) {
        if (a.managedBytes != b.managedBytes) return a.managedBytes < b.managedBytes;
        return a.id.toString(QUuid::WithoutBraces) < b.id.toString(QUuid::WithoutBraces);
    });
    QStringList result;
    for (const auto& n : ns) {
        const auto id = n.id.toString(QUuid::WithoutBraces);
        if (excludeNodeIds.contains(id) || id == localNodeIdString()) continue;
        if (!network_.isNodeConnected(id)) continue;
        if (n.quotaBytes > 0 && n.managedBytes + fileSize > n.quotaBytes) continue;
        if (n.availableDiskBytes > 0 && fileSize + (16LL * 1024 * 1024) > n.availableDiskBytes) continue;
        result.append(id);
        if (result.size() >= count) break;
    }
    return result;
}

void PandoraController::startReplicationForFile(const FileInfo& f) {
    if (f.tombstone) return;
    const int desiredTotal = qMax(1, metadata_.session().replicationFactor);
    const int currentCount = f.replicas.size();
    if (currentCount >= desiredTotal) return;

    const int needed = desiredTotal - currentCount;
    const QStringList targets = chooseReplicas(needed, f.size, f.replicas);
    for (const QString& targetId : targets) {
        pushReplicaToNode(f, targetId, "Replication");
    }
}

void PandoraController::pushReplicaToNode(const FileInfo& f, const QString& targetNodeId, const QString& directionLabel) {
    const QString fileId = f.id.toString(QUuid::WithoutBraces);
    if (!storage_.hasReplica(fileId) || !network_.isNodeConnected(targetNodeId)) return;

    const QString key = fileId + ":" + targetNodeId;
    if (activePushes_.contains(key)) return;

    OutboundPushState st;
    st.transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    st.fileId = fileId;
    st.fileName = f.name;
    st.targetNodeId = targetNodeId;
    st.fileSize = f.size;
    st.sha256 = f.sha256;
    st.nextOffset = 0;
    st.direction = directionLabel;
    activePushes_.insert(key, st);

    network_.sendToNode(targetNodeId, {
        {"kind", "replica_push_begin"},
        {"transferId", st.transferId},
        {"file", f.toJson()}
    });

    if (f.size == 0) {
        network_.sendToNode(targetNodeId, {
            {"kind", "replica_push_end"},
            {"transferId", st.transferId},
            {"fileId", fileId},
            {"sha256", f.sha256}
        });
    } else {
        sendNextPushChunk(key);
    }
}

void PandoraController::sendNextPushChunk(const QString& transferKey) {
    if (!activePushes_.contains(transferKey)) return;
    auto& st = activePushes_[transferKey];

    bool ok = false;
    QString err;
    const QByteArray chunk = storage_.readChunk(st.fileId, st.nextOffset, kTransferChunkBytes, &ok, &err);
    if (!ok) {
        activePushes_.remove(transferKey);
        return;
    }

    const qint64 offsetSent = st.nextOffset;
    st.nextOffset += chunk.size();

    network_.sendToNode(st.targetNodeId, {
        {"kind", "replica_push_chunk"},
        {"transferId", st.transferId},
        {"fileId", st.fileId},
        {"offset", QString::number(offsetSent)},
        {"data", QString::fromLatin1(chunk.toBase64())}
    });

    TransferProgress tp;
    tp.transferId = st.transferId;
    tp.fileId = st.fileId;
    tp.fileName = st.fileName;
    tp.peerNodeId = st.targetNodeId;
    tp.direction = st.direction;
    tp.bytesTransferred = st.nextOffset;
    tp.totalBytes = st.fileSize;
    tp.completed = (st.nextOffset >= st.fileSize);
    emit transferProgressUpdated(tp);

    if (st.nextOffset >= st.fileSize) {
        network_.sendToNode(st.targetNodeId, {
            {"kind", "replica_push_end"},
            {"transferId", st.transferId},
            {"fileId", st.fileId},
            {"sha256", st.sha256}
        });
    }
}

void PandoraController::requestPullFromNode(const FileInfo& f, const QString& sourceNodeId, const QString& exportDest) {
    const QString fileId = f.id.toString(QUuid::WithoutBraces);
    QString err;
    if (!storage_.beginChunkedWrite(fileId, f.size, &err)) {
        emit logLine("Could not allocate local staging space for download: " + err);
        return;
    }

    InboundPullState pull;
    pull.transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pull.fileId = fileId;
    pull.fileName = f.name;
    pull.sourceNodeId = sourceNodeId;
    pull.exportDestination = exportDest;
    pull.fileSize = f.size;
    pull.sha256 = f.sha256;
    pull.nextOffset = 0;
    activePulls_.insert(fileId, pull);

    network_.sendToNode(sourceNodeId, {
        {"kind", "file_pull_request"},
        {"transferId", pull.transferId},
        {"fileId", fileId},
        {"offset", "0"},
        {"maxBytes", QString::number(kTransferChunkBytes)}
    });
}

void PandoraController::onMessage(const QJsonObject& m, const QString& peerNodeId, QTcpSocket* socket, const QString& peerAddress) {
    const QString kind = m.value("kind").toString();

    // 1. Authentication Handshake: Host receives join_request
    if (kind == "join_request") {
        if (!active_) return;
        const QString reqPandoraId = m.value("pandoraId").toString();
        const PandoraSession s = metadata_.session();
        if (!reqPandoraId.isEmpty() && reqPandoraId != s.id.toString(QUuid::WithoutBraces)) {
            network_.disconnectSocket(socket);
            return;
        }

        PendingChallenge ch;
        ch.nonce = SessionSecurity::createNonce();
        ch.clientNodeId = m.value("nodeId").toString();
        ch.clientName = m.value("name").toString();
        ch.clientListenPort = static_cast<quint16>(m.value("listenPort").toInt());
        ch.clientQuotaBytes = m.value("quotaBytes").toString().toLongLong();
        ch.clientAvailableBytes = m.value("availableDiskBytes").toString().toLongLong();
        ch.issuedAtUtc = QDateTime::currentDateTimeUtc();
        pendingChallenges_.insert(socket, ch);

        QJsonObject challengeMsg{
            {"kind", "join_challenge"},
            {"pandoraId", s.id.toString(QUuid::WithoutBraces)},
            {"pandoraName", s.name},
            {"hostNodeId", localNodeIdString()},
            {"salt", QString::fromLatin1(s.salt.toHex())},
            {"nonce", QString::fromLatin1(ch.nonce.toHex())}
        };
        QByteArray bytes = QJsonDocument(challengeMsg).toJson(QJsonDocument::Compact) + '\n';
        socket->write(bytes);
        return;
    }

    // 2. Authentication Handshake: Joining client receives join_challenge
    if (kind == "join_challenge") {
        if (!pendingJoin_.active) return;
        const QString pandoraId = m.value("pandoraId").toString();
        const QByteArray salt = QByteArray::fromHex(m.value("salt").toString().toLatin1());
        const QByteArray nonce = QByteArray::fromHex(m.value("nonce").toString().toLatin1());

        salt_ = salt;
        masterKey_ = SessionSecurity::deriveMasterKey(pendingJoin_.key, salt_);
        keyVerifier_ = SessionSecurity::deriveVerifier(pendingJoin_.key, salt_);
        transportKey_ = SessionSecurity::deriveTransportKey(masterKey_, pandoraId);

        const QByteArray response = SessionSecurity::computeChallengeResponse(
            keyVerifier_, nonce, localNodeIdString());

        QJsonObject respMsg{
            {"kind", "join_response"},
            {"nodeId", localNodeIdString()},
            {"name", pendingJoin_.userName},
            {"listenPort", static_cast<int>(network_.port())},
            {"quotaBytes", QString::number(storage_.quotaBytes())},
            {"availableDiskBytes", QString::number(storage_.availableDiskBytes())},
            {"response", QString::fromLatin1(response.toHex())}
        };
        QByteArray bytes = QJsonDocument(respMsg).toJson(QJsonDocument::Compact) + '\n';
        socket->write(bytes);
        return;
    }

    // 3. Authentication Handshake: Host verifies join_response
    if (kind == "join_response") {
        if (!active_ || !pendingChallenges_.contains(socket)) return;
        const PendingChallenge ch = pendingChallenges_.take(socket);
        const QByteArray resp = QByteArray::fromHex(m.value("response").toString().toLatin1());
        const PandoraSession s = metadata_.session();

        if (!SessionSecurity::verifyChallengeResponse(s.keyVerifier, ch.nonce, ch.clientNodeId, resp)) {
            QJsonObject rejectMsg{
                {"kind", "join_reject"},
                {"reason", "Invalid Pandora Key. Authentication failed."}
            };
            socket->write(QJsonDocument(rejectMsg).toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
            emit logLine(QString("Rejected unauthorized join attempt from %1 (%2)").arg(ch.clientName, peerAddress));
            network_.disconnectSocket(socket);
            return;
        }

        NodeInfo newPeer;
        newPeer.id = QUuid(ch.clientNodeId);
        newPeer.name = ch.clientName;
        newPeer.address = peerAddress;
        newPeer.port = ch.clientListenPort;
        newPeer.connected = true;
        newPeer.managedBytes = 0;
        newPeer.quotaBytes = ch.clientQuotaBytes > 0 ? ch.clientQuotaBytes : (5LL * 1024 * 1024 * 1024);
        newPeer.availableDiskBytes = ch.clientAvailableBytes;
        newPeer.joinedAt = QDateTime::currentDateTimeUtc();
        newPeer.lastSeenUtc = newPeer.joinedAt;
        newPeer.electionTerm = metadata_.term();
        metadata_.addNode(newPeer);

        // Send join_accept before switching socket to sealed mode so client can initialize session
        QJsonObject acceptMsg{
            {"kind", "join_accept"},
            {"hostNodeId", localNodeIdString()},
            {"hostListenPort", static_cast<int>(network_.port())},
            {"snapshot", metadata_.snapshot()}
        };
        socket->write(QJsonDocument(acceptMsg).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();

        network_.markSocketAuthenticated(socket, ch.clientNodeId, peerAddress, ch.clientListenPort);
        publish(EventType::NodeJoined, {{"node", newPeer.toJson()}});
        emit logLine(QString("Na'vi '%1' (%2:%3) authenticated and joined Pandora.")
                     .arg(newPeer.name, peerAddress).arg(newPeer.port));

        triggerReplicaAuditAndRepair();
        return;
    }

    // 4. Joining client receives join_reject
    if (kind == "join_reject") {
        const QString reason = m.value("reason").toString("Authentication rejected by host.");
        pendingJoin_ = {};
        deactivatePandora(false);
        emit logLine("Join failed: " + reason);
        emit joinFailed(reason);
        return;
    }

    // 5. Joining client receives join_accept
    if (kind == "join_accept") {
        if (!pendingJoin_.active) return;
        const QJsonObject snap = m.value("snapshot").toObject();
        const QString hostNodeId = m.value("hostNodeId").toString();
        const quint16 hostPort = static_cast<quint16>(m.value("hostListenPort").toInt(pendingJoin_.hostPort));

        metadata_.applySnapshot(snap);
        PandoraSession s = metadata_.session();
        s.salt = salt_;
        s.keyVerifier = keyVerifier_;
        s.active = true;
        metadata_.setSession(s);

        network_.setTransportKey(transportKey_);
        network_.markSocketAuthenticated(socket, hostNodeId, peerAddress, hostPort);

        // Ensure host's IP address is concrete in our metadata
        NodeInfo hostInfo = metadata_.node(hostNodeId);
        if (!hostInfo.id.isNull()) {
            hostInfo.address = peerAddress;
            hostInfo.port = hostPort;
            hostInfo.connected = true;
            metadata_.addNode(hostInfo);
        }

        NodeInfo self = metadata_.node(localNodeIdString());
        self.id = localNodeId_;
        self.name = localUserName_;
        self.port = network_.port();
        self.connected = true;
        self.quotaBytes = storage_.quotaBytes();
        self.availableDiskBytes = storage_.availableDiskBytes();
        if (!self.joinedAt.isValid()) self.joinedAt = QDateTime::currentDateTimeUtc();
        self.lastSeenUtc = QDateTime::currentDateTimeUtc();
        metadata_.addNode(self);

        active_ = true;
        pendingJoin_ = {};
        maintenanceTimer_.start();

        // Sync countdown timer if session is already in shutdown warning state
        if (s.shuttingDown && s.shutdownDeadlineUtc.isValid()) {
            const qint64 rem = QDateTime::currentDateTimeUtc().secsTo(s.shutdownDeadlineUtc);
            if (rem > 0) {
                shutdownRemainingSeconds_ = static_cast<int>(rem);
                shutdownTickTimer_.start();
                emit shutdownCountdownUpdated(shutdownRemainingSeconds_);
            }
        }

        connectToKnownPeers();
        emit statusChanged(QString("Pandora Active — %1").arg(s.name));
        emit logLine(QString("Joined Pandora '%1' (Guardian: %2).").arg(s.name, metadata_.guardian().left(8)));
        emit joinSucceeded(s.name);
        return;
    }

    // 6. Peer-to-peer mesh authentication between already-joined members
    if (kind == "peer_hello") {
        if (!active_) return;
        const QString remoteNodeId = m.value("nodeId").toString();
        const QString remoteName = m.value("name").toString();
        const quint16 remotePort = static_cast<quint16>(m.value("listenPort").toInt());
        const QByteArray nonce = QByteArray::fromHex(m.value("nonce").toString().toLatin1());
        const QByteArray proof = QByteArray::fromHex(m.value("proof").toString().toLatin1());

        if (!SessionSecurity::verifyChallengeResponse(keyVerifier_, nonce, remoteNodeId, proof)) {
            network_.disconnectSocket(socket);
            return;
        }

        if (!m.value("isAck").toBool()) {
            const QByteArray myNonce = SessionSecurity::createNonce();
            const QByteArray myProof = SessionSecurity::computeChallengeResponse(keyVerifier_, myNonce, localNodeIdString());
            QJsonObject ack{
                {"kind", "peer_hello"},
                {"isAck", true},
                {"nodeId", localNodeIdString()},
                {"name", localUserName_},
                {"listenPort", static_cast<int>(network_.port())},
                {"nonce", QString::fromLatin1(myNonce.toHex())},
                {"proof", QString::fromLatin1(myProof.toHex())}
            };
            // Write directly on the incoming socket before sealing/deduplicating
            QJsonObject sealedAck = SessionSecurity::sealJson(ack, transportKey_, 0);
            socket->write(QJsonDocument(sealedAck).toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
        }

        network_.markSocketAuthenticated(socket, remoteNodeId, peerAddress, remotePort);

        NodeInfo n = metadata_.node(remoteNodeId);
        n.id = QUuid(remoteNodeId);
        if (!remoteName.isEmpty()) n.name = remoteName;
        n.address = peerAddress;
        n.port = remotePort;
        n.connected = true;
        n.lastSeenUtc = QDateTime::currentDateTimeUtc();
        metadata_.addNode(n);
        return;
    }

    if (!active_) return;

    // 7. Standard authenticated messages
    if (kind == "event") {
        onEvent(PandoraEvent::fromJson(m.value("event").toObject()));
        return;
    }
    if (kind == "snapshot") {
        metadata_.applySnapshot(m.value("data").toObject());
        return;
    }
    if (kind == "snapshot_request") {
        network_.sendToNode(peerNodeId, {{"kind", "snapshot"}, {"data", metadata_.snapshot()}});
        return;
    }

    // 8. Replica Push Receiver
    if (kind == "replica_push_begin") {
        const FileInfo f = FileInfo::fromJson(m.value("file").toObject());
        const QString fileId = f.id.toString(QUuid::WithoutBraces);
        metadata_.applyRemoteFile(f);
        QString err;
        if (!storage_.beginChunkedWrite(fileId, f.size, &err)) {
            network_.sendToNode(peerNodeId, {
                {"kind", "replica_push_nack"},
                {"fileId", fileId},
                {"reason", err}
            });
        }
        return;
    }
    if (kind == "replica_push_chunk") {
        const QString fileId = m.value("fileId").toString();
        const QString transferId = m.value("transferId").toString();
        const qint64 offset = m.value("offset").toString().toLongLong();
        const QByteArray data = QByteArray::fromBase64(m.value("data").toString().toLatin1());
        QString err;
        if (!storage_.writeChunk(fileId, offset, data, &err)) {
            storage_.abortChunkedWrite(fileId);
            network_.sendToNode(peerNodeId, {
                {"kind", "replica_push_nack"},
                {"fileId", fileId},
                {"reason", err}
            });
            return;
        }
        network_.sendToNode(peerNodeId, {
            {"kind", "replica_chunk_ack"},
            {"transferId", transferId},
            {"fileId", fileId},
            {"nextOffset", QString::number(offset + data.size())}
        });
        return;
    }
    if (kind == "replica_chunk_ack") {
        const QString fileId = m.value("fileId").toString();
        const QString key = fileId + ":" + peerNodeId;
        if (activePushes_.contains(key)) {
            if (activePushes_[key].nextOffset < activePushes_[key].fileSize) {
                sendNextPushChunk(key);
            }
        }
        return;
    }
    if (kind == "replica_push_end") {
        const QString fileId = m.value("fileId").toString();
        const QString sha256 = m.value("sha256").toString();
        QString err;
        if (!storage_.finalizeChunkedWrite(fileId, sha256, &err)) {
            network_.sendToNode(peerNodeId, {
                {"kind", "replica_push_nack"},
                {"fileId", fileId},
                {"reason", err}
            });
            return;
        }
        metadata_.addReplicaToFile(fileId, localNodeIdString());
        network_.sendToNode(peerNodeId, {
            {"kind", "replica_commit_ack"},
            {"fileId", fileId},
            {"replicaNodeId", localNodeIdString()}
        });
        publish(EventType::ReplicaChanged, {
            {"fileId", fileId},
            {"replicaNodeId", localNodeIdString()},
            {"hasReplica", true}
        });
        const FileInfo f = metadata_.file(fileId);
        emit logLine(QString("Verified and stored replica of '%1' (%2 bytes).").arg(f.name).arg(f.size));
        return;
    }
    if (kind == "replica_commit_ack") {
        const QString fileId = m.value("fileId").toString();
        const QString repNode = m.value("replicaNodeId").toString(peerNodeId);
        activePushes_.remove(fileId + ":" + repNode);
        metadata_.addReplicaToFile(fileId, repNode);
        publish(EventType::ReplicaChanged, {
            {"fileId", fileId},
            {"replicaNodeId", repNode},
            {"hasReplica", true}
        });
        const FileInfo f = metadata_.file(fileId);
        emit logLine(QString("Replica of '%1' confirmed on peer %2.").arg(f.name, repNode.left(8)));
        return;
    }
    if (kind == "replica_push_nack") {
        const QString fileId = m.value("fileId").toString();
        activePushes_.remove(fileId + ":" + peerNodeId);
        emit logLine(QString("Peer %1 rejected replica for %2: %3")
                     .arg(peerNodeId.left(8), fileId.left(8), m.value("reason").toString()));
        return;
    }

    // 9. Remote File Pull (Download / Self-Healing Pull)
    if (kind == "file_pull_request") {
        const QString fileId = m.value("fileId").toString();
        const QString transferId = m.value("transferId").toString();
        const qint64 offset = m.value("offset").toString().toLongLong();
        const qint64 maxBytes = m.value("maxBytes").toString().toLongLong();

        if (!storage_.hasReplica(fileId)) {
            network_.sendToNode(peerNodeId, {
                {"kind", "file_pull_error"},
                {"fileId", fileId},
                {"reason", "Replica not present on this node."}
            });
            return;
        }
        bool ok = false;
        QString err;
        const QByteArray chunk = storage_.readChunk(fileId, offset, qMin<qint64>(maxBytes, kTransferChunkBytes), &ok, &err);
        if (!ok) {
            network_.sendToNode(peerNodeId, {
                {"kind", "file_pull_error"},
                {"fileId", fileId},
                {"reason", err}
            });
            return;
        }
        const FileInfo f = metadata_.file(fileId);
        const bool eof = (offset + chunk.size() >= f.size);
        network_.sendToNode(peerNodeId, {
            {"kind", "file_pull_chunk"},
            {"transferId", transferId},
            {"fileId", fileId},
            {"offset", QString::number(offset)},
            {"data", QString::fromLatin1(chunk.toBase64())},
            {"eof", eof},
            {"sha256", f.sha256},
            {"totalSize", QString::number(f.size)}
        });
        return;
    }

    if (kind == "file_pull_chunk") {
        const QString fileId = m.value("fileId").toString();
        if (!activePulls_.contains(fileId)) return;
        auto& pull = activePulls_[fileId];

        const qint64 offset = m.value("offset").toString().toLongLong();
        const QByteArray chunk = QByteArray::fromBase64(m.value("data").toString().toLatin1());
        const bool eof = m.value("eof").toBool();

        QString err;
        if (!storage_.writeChunk(fileId, offset, chunk, &err)) {
            storage_.abortChunkedWrite(fileId);
            activePulls_.remove(fileId);
            emit logLine("Remote download write failed: " + err);
            return;
        }

        pull.nextOffset = offset + chunk.size();

        TransferProgress tp;
        tp.transferId = pull.transferId;
        tp.fileId = pull.fileId;
        tp.fileName = pull.fileName;
        tp.peerNodeId = peerNodeId;
        tp.direction = "Download";
        tp.bytesTransferred = pull.nextOffset;
        tp.totalBytes = pull.fileSize;
        tp.completed = eof;
        emit transferProgressUpdated(tp);

        if (!eof) {
            network_.sendToNode(peerNodeId, {
                {"kind", "file_pull_request"},
                {"transferId", pull.transferId},
                {"fileId", fileId},
                {"offset", QString::number(pull.nextOffset)},
                {"maxBytes", QString::number(kTransferChunkBytes)}
            });
        } else {
            if (!storage_.finalizeChunkedWrite(fileId, pull.sha256, &err)) {
                activePulls_.remove(fileId);
                emit logLine("Downloaded file failed SHA-256 integrity check: " + err);
                return;
            }
            const QString dest = pull.exportDestination;
            const QString fname = pull.fileName;
            activePulls_.remove(fileId);

            // We now also hold a verified local replica!
            metadata_.addReplicaToFile(fileId, localNodeIdString());
            publish(EventType::ReplicaChanged, {
                {"fileId", fileId},
                {"replicaNodeId", localNodeIdString()},
                {"hasReplica", true}
            });

            if (!dest.isEmpty()) {
                if (storage_.exportFile(fileId, dest, &err)) {
                    emit logLine(QString("Downloaded and verified '%1' to %2.").arg(fname, dest));
                    emit remoteDownloadCompleted(fileId, dest);
                } else {
                    emit logLine(QString("Export failed for '%1': %2").arg(fname, err));
                }
            }
        }
        return;
    }

    if (kind == "file_pull_error") {
        const QString fileId = m.value("fileId").toString();
        if (!activePulls_.contains(fileId)) return;
        const auto failedPull = activePulls_.take(fileId);
        storage_.abortChunkedWrite(fileId);

        // Fallback to another online replica peer if available
        const FileInfo f = metadata_.file(fileId);
        for (const QString& altPeer : f.replicas) {
            if (altPeer != localNodeIdString() && altPeer != peerNodeId && network_.isNodeConnected(altPeer)) {
                emit logLine(QString("Falling back to replica peer %1 for '%2'...").arg(altPeer.left(8), f.name));
                requestPullFromNode(f, altPeer, failedPull.exportDestination);
                return;
            }
        }
        emit logLine(QString("Remote download failed for '%1': %2").arg(failedPull.fileName, m.value("reason").toString()));
        return;
    }
}

void PandoraController::onSessionDiscovered(const DiscoveredSession& session) {
    discoveredSessions_.insert(session.pandoraId, session);
    emit discoveredSessionsChanged();

    // If active in the same Pandora session, ensure mesh connection to discovered peer
    if (active_ && session.pandoraId == metadata_.session().id.toString(QUuid::WithoutBraces)) {
        if (!session.hostNodeId.isEmpty() && session.hostNodeId != localNodeIdString() &&
            !network_.isNodeConnected(session.hostNodeId)) {
            const QByteArray nonce = SessionSecurity::createNonce();
            const QByteArray proof = SessionSecurity::computeChallengeResponse(keyVerifier_, nonce, localNodeIdString());
            QJsonObject hello{
                {"kind", "peer_hello"},
                {"isAck", false},
                {"nodeId", localNodeIdString()},
                {"name", localUserName_},
                {"listenPort", static_cast<int>(network_.port())},
                {"nonce", QString::fromLatin1(nonce.toHex())},
                {"proof", QString::fromLatin1(proof.toHex())}
            };
            network_.sendToAddress(session.address, session.port, hello, session.hostNodeId);
        }
    }
}

void PandoraController::onPeerConnected(const QString& nodeId, const QString& address, quint16 port) {
    if (!active_) return;
    NodeInfo n = metadata_.node(nodeId);
    if (!n.id.isNull()) {
        n.connected = true;
        if (!address.isEmpty()) n.address = address;
        if (port > 0) n.port = port;
        n.lastSeenUtc = QDateTime::currentDateTimeUtc();
        metadata_.addNode(n);
    }
    triggerReplicaAuditAndRepair();
}

void PandoraController::onPeerDisconnected(const QString& nodeId) {
    if (!active_) return;
    const NodeInfo peer = metadata_.node(nodeId);
    metadata_.updateNodeStatus(nodeId, false);
    emit logLine(QString("Na'vi '%1' (%2) disconnected.").arg(peer.name.isEmpty() ? nodeId.left(8) : peer.name, nodeId.left(8)));

    // Clean up any in-flight pushes to this peer
    const auto pushKeys = activePushes_.keys();
    for (const QString& k : pushKeys) {
        if (k.endsWith(":" + nodeId)) activePushes_.remove(k);
    }

    // Re-route any in-flight pulls from this peer to another replica
    const auto pullKeys = activePulls_.keys();
    for (const QString& fileId : pullKeys) {
        if (activePulls_[fileId].sourceNodeId == nodeId) {
            const auto pull = activePulls_.take(fileId);
            storage_.abortChunkedWrite(fileId);
            const FileInfo f = metadata_.file(fileId);
            for (const QString& altPeer : f.replicas) {
                if (altPeer != localNodeIdString() && altPeer != nodeId && network_.isNodeConnected(altPeer)) {
                    requestPullFromNode(f, altPeer, pull.exportDestination);
                    break;
                }
            }
        }
    }

    checkGuardianHealthAndElect();
    triggerReplicaAuditAndRepair();
}

void PandoraController::onEvent(const PandoraEvent& e) {
    if (e.originNodeId == localNodeIdString()) return;
    if (!metadata_.recordEvent(e)) {
        // Deduplicate already-seen events
        return;
    }

    // Gossip relay to other connected peers so full mesh stays synchronized
    network_.broadcastEvent(e);

    switch (e.type) {
    case EventType::NodeJoined:
    case EventType::NodeUpdated: {
        const NodeInfo n = NodeInfo::fromJson(e.payload.value("node").toObject());
        metadata_.addNode(n);
        connectToKnownPeers();
        triggerReplicaAuditAndRepair();
        break;
    }
    case EventType::NodeLeft: {
        const QString leftId = e.payload.value("nodeId").toString();
        metadata_.updateNodeStatus(leftId, false);
        checkGuardianHealthAndElect();
        triggerReplicaAuditAndRepair();
        break;
    }
    case EventType::FileAdded:
    case EventType::FileUpdated: {
        const FileInfo f = FileInfo::fromJson(e.payload.value("file").toObject());
        metadata_.applyRemoteFile(f);
        break;
    }
    case EventType::FileDeleted: {
        const QString fileId = e.payload.value("fileId").toString();
        if (e.payload.contains("file")) {
            metadata_.applyRemoteFile(FileInfo::fromJson(e.payload.value("file").toObject()));
        } else {
            QString ignored;
            metadata_.deleteFile(fileId, QUuid(e.originNodeId), &ignored);
        }
        storage_.removeFile(fileId, true);
        emit logLine(QString("Removed local replica for deleted file (%1).").arg(fileId.left(8)));
        break;
    }
    case EventType::ReplicaChanged: {
        const QString fileId = e.payload.value("fileId").toString();
        const QString repNode = e.payload.value("replicaNodeId").toString();
        const bool hasRep = e.payload.value("hasReplica").toBool(true);
        if (hasRep) metadata_.addReplicaToFile(fileId, repNode);
        else metadata_.removeReplicaFromFile(fileId, repNode);
        break;
    }
    case EventType::GuardianChanged: {
        const QString newGuardian = e.payload.value("guardian").toString();
        const quint64 term = e.payload.value("term").toString("1").toULongLong();
        metadata_.setGuardian(newGuardian, term);
        const PandoraSession s = metadata_.session();
        emit statusChanged(QString("Pandora Active — %1%2")
                           .arg(s.name, isGuardian() ? " (Guardian)" : ""));
        emit logLine(QString("Guardian elected: %1 (term %2).").arg(newGuardian.left(8)).arg(term));
        break;
    }
    case EventType::ShutdownWarning: {
        const QString senderGuardian = e.payload.value("guardianNodeId").toString(e.originNodeId);
        if (senderGuardian != metadata_.guardian()) break;
        const int secs = e.payload.value("secondsRemaining").toInt(120);
        shutdownRemainingSeconds_ = secs;
        PandoraSession s = metadata_.session();
        s.shuttingDown = true;
        s.shutdownDeadlineUtc = QDateTime::fromString(e.payload.value("deadlineUtc").toString(), Qt::ISODateWithMs);
        metadata_.setSession(s);
        shutdownTickTimer_.start();
        emit shutdownCountdownUpdated(shutdownRemainingSeconds_);
        emit statusChanged(QString("Shutdown Countdown: %1s remaining").arg(shutdownRemainingSeconds_));
        emit logLine(QString("WARNING: Guardian initiated Pandora shutdown in %1 seconds! Use 'Download All' to save files.").arg(secs));
        break;
    }
    case EventType::ShutdownCancelled: {
        const QString senderGuardian = e.payload.value("guardianNodeId").toString(e.originNodeId);
        if (senderGuardian != metadata_.guardian()) break;
        shutdownTickTimer_.stop();
        shutdownRemainingSeconds_ = 0;
        PandoraSession s = metadata_.session();
        s.shuttingDown = false;
        s.shutdownDeadlineUtc = {};
        metadata_.setSession(s);
        emit shutdownCountdownUpdated(0);
        emit statusChanged(QString("Pandora Active — %1").arg(s.name));
        emit logLine("Guardian cancelled the session shutdown countdown.");
        break;
    }
    case EventType::MetadataSnapshot:
        metadata_.applySnapshot(e.payload);
        break;
    case EventType::SessionDeactivated: {
        const QString senderGuardian = e.payload.value("guardianNodeId").toString(e.originNodeId);
        if (senderGuardian == metadata_.guardian()) {
            emit logLine("Guardian deactivated Pandora. Cleaning up local temporary storage.");
            deactivatePandora(false);
        }
        break;
    }
    default:
        break;
    }
}

void PandoraController::connectToKnownPeers() {
    if (!active_) return;
    const QString myId = localNodeIdString();
    for (const NodeInfo& n : metadata_.nodes()) {
        const QString nid = n.id.toString(QUuid::WithoutBraces);
        if (nid == myId || !n.connected) continue;
        if (network_.isNodeConnected(nid)) continue;
        if (n.address.isEmpty() || n.port == 0) continue;

        const QByteArray nonce = SessionSecurity::createNonce();
        const QByteArray proof = SessionSecurity::computeChallengeResponse(keyVerifier_, nonce, myId);
        QJsonObject hello{
            {"kind", "peer_hello"},
            {"isAck", false},
            {"nodeId", myId},
            {"name", localUserName_},
            {"listenPort", static_cast<int>(network_.port())},
            {"nonce", QString::fromLatin1(nonce.toHex())},
            {"proof", QString::fromLatin1(proof.toHex())}
        };
        network_.sendToAddress(n.address, n.port, hello, nid);
    }
}

void PandoraController::checkGuardianHealthAndElect() {
    if (!active_) return;
    const QString currentGuardian = metadata_.guardian();
    if (currentGuardian == localNodeIdString()) return;
    if (network_.isNodeConnected(currentGuardian)) return;

    // Current Guardian is disconnected; run deterministic election
    metadata_.updateNodeStatus(currentGuardian, false);
    const QString elected = metadata_.electDeterministicGuardian();
    if (elected.isEmpty()) return;

    const quint64 nextTerm = metadata_.term() + 1;
    metadata_.setGuardian(elected, nextTerm);
    const PandoraSession s = metadata_.session();

    if (elected == localNodeIdString()) {
        publish(EventType::GuardianChanged, {
            {"guardian", elected},
            {"term", QString::number(nextTerm)}
        });
        emit statusChanged(QString("Pandora Active — %1 (Guardian)").arg(s.name));
        emit logLine(QString("Previous Guardian disconnected. Local node elected as new Guardian (term %1).").arg(nextTerm));
    } else {
        emit logLine(QString("Previous Guardian disconnected. Peer %1 elected as new Guardian (term %2).")
                     .arg(elected.left(8)).arg(nextTerm));
    }
}

void PandoraController::triggerReplicaAuditAndRepair() {
    if (!active_ || isShuttingDown()) return;

    const int targetFactor = qMax(1, metadata_.session().replicationFactor);
    const QString myId = localNodeIdString();

    for (const FileInfo& f : metadata_.files(false)) {
        const QString fid = f.id.toString(QUuid::WithoutBraces);

        // Verify our own local replica integrity if we claim to hold it
        if (f.replicas.contains(myId)) {
            if (!storage_.verifyReplica(fid, f.sha256, f.size)) {
                storage_.removeFile(fid, false);
                metadata_.removeReplicaFromFile(fid, myId);
                publish(EventType::ReplicaChanged, {
                    {"fileId", fid},
                    {"replicaNodeId", myId},
                    {"hasReplica", false}
                });
                emit logLine(QString("Detected corrupted/missing local replica for '%1'; scheduled self-healing.").arg(f.name));
                continue;
            }
        }

        // Count online verified replicas
        QStringList onlineReplicas;
        for (const QString& r : f.replicas) {
            if (r == myId || network_.isNodeConnected(r)) {
                onlineReplicas.append(r);
            }
        }

        // If we hold a verified replica and the file is under-replicated, push to available peers
        // Deterministic coordinator for repair: the first online replica holder in sorted order
        QStringList sortedHolders = onlineReplicas;
        sortedHolders.sort();
        if (!sortedHolders.isEmpty() && sortedHolders.first() == myId && onlineReplicas.size() < targetFactor) {
            const int needed = targetFactor - onlineReplicas.size();
            const QStringList targets = chooseReplicas(needed, f.size, onlineReplicas);
            for (const QString& targetNode : targets) {
                pushReplicaToNode(f, targetNode, "Repair");
            }
        }
    }
}

void PandoraController::onPeriodicMaintenance() {
    if (!active_) return;

    const PandoraSession s = metadata_.session();
    network_.announce({
        {"pandoraId", s.id.toString(QUuid::WithoutBraces)},
        {"port", static_cast<int>(network_.port())},
        {"name", s.name},
        {"hostName", localUserName_},
        {"nodeCount", metadata_.nodes(true).size()}
    });

    connectToKnownPeers();
    checkGuardianHealthAndElect();
    triggerReplicaAuditAndRepair();
}

void PandoraController::onShutdownTick() {
    if (!active_ || shutdownRemainingSeconds_ <= 0) {
        shutdownTickTimer_.stop();
        return;
    }
    --shutdownRemainingSeconds_;
    emit shutdownCountdownUpdated(shutdownRemainingSeconds_);
    emit statusChanged(QString("Shutdown Countdown: %1s remaining").arg(shutdownRemainingSeconds_));

    if (shutdownRemainingSeconds_ <= 0) {
        shutdownTickTimer_.stop();
        emit logLine("Shutdown countdown reached zero. Deactivating Pandora and wiping local temporary storage.");
        deactivatePandora(isGuardian());
    }
}

} // namespace pandora
