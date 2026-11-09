#include "security/session_security.h"
#include <QCryptographicHash>
#include <QRandomGenerator>

namespace pandora {
QByteArray SessionSecurity::createSalt() {
    QByteArray salt(32, Qt::Uninitialized);
    for (qsizetype i = 0; i < salt.size(); i += 4) {
        quint32 r = QRandomGenerator::system()->generate();
        for (int b = 0; b < 4 && i + b < salt.size(); ++b)
            salt[i + b] = static_cast<char>((r >> (b * 8)) & 0xff);
    }
    return salt;
}
QByteArray SessionSecurity::deriveVerifier(const QString& key, const QByteArray& salt) {
    // Demo-grade salted SHA-256. Replace with Argon2id/scrypt for production.
    return QCryptographicHash::hash(salt + key.toUtf8(), QCryptographicHash::Sha256);
}
bool SessionSecurity::verify(const QString& key, const QByteArray& salt, const QByteArray& verifier) {
    return deriveVerifier(key, salt) == verifier;
}
QByteArray SessionSecurity::randomSessionKey() {
    return createSalt().toHex();
}
}
