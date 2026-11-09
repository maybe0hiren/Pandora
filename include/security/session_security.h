#pragma once
#include <QByteArray>
#include <QString>

namespace pandora {
class SessionSecurity final {
public:
    static QByteArray createSalt();
    static QByteArray deriveVerifier(const QString& key, const QByteArray& salt);
    static bool verify(const QString& key, const QByteArray& salt, const QByteArray& verifier);
    static QByteArray randomSessionKey();
};
}
