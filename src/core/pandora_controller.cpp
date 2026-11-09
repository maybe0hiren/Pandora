#include "core/pandora_controller.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>

namespace pandora {
PandoraController::PandoraController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<PandoraEvent>();
    connect(&network_, &PeerNode::messageReceived, this, &PandoraController::onMessage);
    connect(&network_, &PeerNode::peerDiscovered, this, &PandoraController::onPeerDiscovered);
    connect(&eventBus_, &EventBus::eventPublished, this, &PandoraController::onEvent);
    connect(&storage_, &StorageManager::storageChanged, this, [this](qint64 bytes) {
        auto ns = metadata_.nodes();
        for (auto n : ns) if (n.id == localNodeId_) { n.managedBytes = bytes; metadata_.addNode(n); break; }
    });
    shutdownTimer_.setSingleShot(true);
    connect(&shutdownTimer_, &QTimer::timeout, this, &PandoraController::deactivatePandora);
}
bool PandoraController::createPandora(const QString& name, const QString& user, const QString& key, QString* error) {
    if (active_) { if (error) *error = "Pandora is already active."; return false; }
    if (name.trimmed().isEmpty() || user.trimmed().isEmpty() || key.size() < 6) {
        if (error) *error = "Enter a Pandora name, your name, and a key of at least 6 characters."; return false;
    }
    if (!storage_.initialize()) { if (error) *error = "Could not initialize temporary storage."; return false; }
    if (!network_.start(tcpPort_)) { storage_.destroySessionData(); if (error) *error = "Could not start LAN networking. Check ports/firewall."; return false; }
    salt_ = SessionSecurity::createSalt();
    PandoraSession s; s.id = QUuid::createUuid(); s.name = name.trimmed();
    s.keyVerifier = SessionSecurity::deriveVerifier(key, salt_); s.active = true;
    metadata_.setSession(s); metadata_.setGuardian(localNodeIdString());
    NodeInfo self{localNodeId_, user.trimmed(), {}, tcpPort_, true, 0}; metadata_.addNode(self);
    active_ = true;
    publish(EventType::SessionCreated, {{"pandoraId", s.id.toString(QUuid::WithoutBraces)}, {"name", s.name}});
    network_.announce({{"pandoraId", s.id.toString(QUuid::WithoutBraces)}, {"port", tcpPort_}, {"name", s.name}});
    emit statusChanged("Pandora Active"); emit logLine("Pandora created. Local node is Guardian.");
    return true;
}
bool PandoraController::joinPandora(const QString& user, const QString& key, const QString& pandoraId, QString* error) {
    Q_UNUSED(pandoraId)
    if (user.trimmed().isEmpty() || key.isEmpty()) { if (error) *error = "Enter your name and Pandora Key."; return false; }
    if (error) *error = "Join handshake is not yet enabled in this starter build. LAN discovery is implemented; secure join requires a host challenge/response and key-verifier exchange.";
    return false;
}
bool PandoraController::uploadFile(const QString& path, QString* error) {
    if (!active_) { if (error) *error = "Pandora is not active."; return false; }
    QFileInfo fi(path); if (!fi.exists() || !fi.isFile()) { if (error) *error = "Choose a valid file."; return false; }
    for (const auto& f : metadata_.files()) if (f.name.compare(fi.fileName(), Qt::CaseInsensitive) == 0) {
        if (error) *error = "Duplicate filenames are not allowed."; return false;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!storage_.storeFile(path, id, error)) return false;
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { storage_.removeFile(id); if (error) *error = file.errorString(); return false; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    FileInfo f; f.id = QUuid(id); f.name = fi.fileName(); f.size = fi.size(); f.sha256 = hash.result().toHex();
    f.modifiedBy = localNodeId_; f.modifiedAt = QDateTime::currentDateTimeUtc(); f.replicas = {localNodeIdString()};
    QString why; if (!metadata_.addFile(f, &why)) { storage_.removeFile(id); if (error) *error = why; return false; }
    publish(EventType::FileAdded, {{"file", f.toJson()}});
    emit logLine("Uploaded " + f.name + " to local temporary storage. Replication requires connected peers.");
    return true;
}
bool PandoraController::downloadFile(const QString& id, const QString& dest, QString* error) {
    auto fs = metadata_.files();
    for (const auto& f : fs) if (f.id.toString(QUuid::WithoutBraces) == id) {
        if (f.replicas.contains(localNodeIdString())) return storage_.exportFile(id, dest, error);
        if (error) *error = "No remote file-transfer path is available in this starter build.";
        return false;
    }
    if (error) *error = "File not found."; return false;
}
bool PandoraController::deleteFile(const QString& id, QString* error) {
    if (!metadata_.deleteFile(id, localNodeId_, error)) return false;
    publish(EventType::FileDeleted, {{"fileId", id}});
    emit logLine("File deleted from metadata. Replica cleanup propagation is not yet implemented.");
    return true;
}
void PandoraController::deactivatePandora() {
    if (!active_) return;
    publish(EventType::SessionDeactivated, {});
    network_.stop(); storage_.destroySessionData(); metadata_.reset();
    active_ = false; salt_.clear();
    emit statusChanged("Pandora Inactive"); emit logLine("Pandora session data removed.");
}
bool PandoraController::isActive() const { return active_; }
void PandoraController::publish(EventType type, const QJsonObject& payload) {
    PandoraEvent e; e.type = type; e.originNodeId = localNodeIdString(); e.payload = payload;
    eventBus_.publish(e); network_.broadcastEvent(e);
}
QStringList PandoraController::chooseReplicas(int count, const QString& exclude) const {
    auto ns = metadata_.nodes();
    std::sort(ns.begin(), ns.end(), [](const NodeInfo& a, const NodeInfo& b) { return a.managedBytes < b.managedBytes; });
    QStringList result;
    for (const auto& n : ns) {
        const auto id = n.id.toString(QUuid::WithoutBraces);
        if (n.connected && id != exclude && result.size() < count) result.append(id);
    }
    return result;
}
void PandoraController::onMessage(const QJsonObject& m, const QString& address) {
    Q_UNUSED(address)
    if (m.value("kind").toString() == "event") onEvent(PandoraEvent::fromJson(m.value("event").toObject()));
    else if (m.value("kind").toString() == "snapshot") metadata_.applySnapshot(m.value("data").toObject());
}
void PandoraController::onPeerDiscovered(const QString& address, quint16 port, const QString& pandoraId) {
    if (!active_) return;
    const auto s = metadata_.session();
    if (pandoraId != s.id.toString(QUuid::WithoutBraces)) return;
    network_.sendTo(address, port, {{"kind", "snapshot"}, {"data", metadata_.snapshot()}});
}
void PandoraController::onEvent(const PandoraEvent& e) {
    if (e.originNodeId == localNodeIdString()) return;
    switch (e.type) {
    case EventType::FileAdded:
    case EventType::FileUpdated:
        metadata_.applyRemoteFile(FileInfo::fromJson(e.payload.value("file").toObject())); break;
    case EventType::FileDeleted: {
        QString ignored; metadata_.deleteFile(e.payload.value("fileId").toString(), QUuid(e.originNodeId), &ignored); break;
    }
    case EventType::GuardianChanged:
        metadata_.setGuardian(e.payload.value("guardian").toString()); break;
    case EventType::MetadataSnapshot:
        metadata_.applySnapshot(e.payload); break;
    case EventType::SessionDeactivated:
        deactivatePandora(); break;
    default: break;
    }
}
}
