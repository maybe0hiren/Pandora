#pragma once
#include "core/event_bus.h"
#include "metadata/metadata_store.h"
#include "network/peer_node.h"
#include "security/session_security.h"
#include "storage/storage_manager.h"
#include <QObject>
#include <QTimer>

namespace pandora {
class PandoraController final : public QObject {
    Q_OBJECT
public:
    explicit PandoraController(QObject* parent = nullptr);
    MetadataStore* metadata() { return &metadata_; }
    StorageManager* storage() { return &storage_; }
    PeerNode* network() { return &network_; }
    QUuid localNodeId() const { return localNodeId_; }
    QString localNodeIdString() const { return localNodeId_.toString(QUuid::WithoutBraces); }
    bool createPandora(const QString& pandoraName, const QString& userName, const QString& key, QString* error = nullptr);
    bool joinPandora(const QString& userName, const QString& key, const QString& pandoraId, QString* error = nullptr);
    bool uploadFile(const QString& path, QString* error = nullptr);
    bool downloadFile(const QString& fileId, const QString& destination, QString* error = nullptr);
    bool deleteFile(const QString& fileId, QString* error = nullptr);
    void deactivatePandora();
    bool isActive() const;
signals:
    void statusChanged(const QString& status);
    void logLine(const QString& line);
private slots:
    void onMessage(const QJsonObject& message, const QString& peerAddress);
    void onPeerDiscovered(const QString& address, quint16 port, const QString& pandoraId);
    void onEvent(const PandoraEvent& event);
private:
    void publish(EventType type, const QJsonObject& payload);
    QStringList chooseReplicas(int count, const QString& exclude = {}) const;
    MetadataStore metadata_;
    StorageManager storage_;
    PeerNode network_;
    EventBus eventBus_;
    QUuid localNodeId_{QUuid::createUuid()};
    QByteArray salt_;
    QTimer shutdownTimer_;
    quint16 tcpPort_{43170};
    bool active_{false};
};
}
