#pragma once
#include "core/types.h"
#include <QObject>

namespace pandora {
class EventBus final : public QObject {
    Q_OBJECT
public:
    explicit EventBus(QObject* parent = nullptr) : QObject(parent) {}
    void publish(const PandoraEvent& event) { emit eventPublished(event); }
signals:
    void eventPublished(const pandora::PandoraEvent& event);
};
}
Q_DECLARE_METATYPE(pandora::PandoraEvent)
