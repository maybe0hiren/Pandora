#pragma once
#include "core/types.h"
#include <QHash>
#include <QMutex>
#include <QObject>

namespace pandora {
class MetadataStore final : public QObject {
    Q_OBJECT
public:
    explicit MetadataStore(QObject* parent = nullptr);
    void reset();
    bool addNode(const NodeInfo& node);
    void removeNode(const QString& nodeId);
    QList<NodeInfo> nodes() const;
    bool addFile(const FileInfo& file, QString* error = nullptr);
    bool updateFile(const FileInfo& file, QString* error = nullptr);
    bool deleteFile(const QString& fileId, const QUuid& actor, QString* error = nullptr);
    QList<FileInfo> files(bool includeDeleted = false) const;
    bool applyRemoteFile(const FileInfo& file);
    void setGuardian(const QString& nodeId);
    QString guardian() const;
    void setSession(const PandoraSession& session);
    PandoraSession session() const;
    QJsonObject snapshot() const;
    void applySnapshot(const QJsonObject& snapshot);
signals:
    void changed();
private:
    mutable QMutex mutex_;
    QHash<QString, NodeInfo> nodes_;
    QHash<QString, FileInfo> files_;
    QString guardian_;
    PandoraSession session_;
};
}
