#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace pandora {

class SessionSecurity final {
public:
    static constexpr int kSaltBytes = 32;
    static constexpr int kNonceBytes = 24;
    static constexpr int kKeyBytes = 32;
    static constexpr int kPbkdf2Iterations = 60000;

    static QByteArray createSalt(int bytes = kSaltBytes);
    static QByteArray createNonce();

    // Strong PBKDF2-HMAC-SHA256 key derivation
    static QByteArray deriveMasterKey(const QString& passkey, const QByteArray& salt, int iterations = kPbkdf2Iterations);
    static QByteArray deriveVerifier(const QString& passkey, const QByteArray& salt);
    static QByteArray deriveTransportKey(const QByteArray& masterKey, const QString& pandoraId);

    static bool constantTimeEquals(const QByteArray& a, const QByteArray& b);
    static bool verify(const QString& passkey, const QByteArray& salt, const QByteArray& verifier);

    // Challenge-Response Authentication
    static QByteArray computeChallengeResponse(const QByteArray& verifier, const QByteArray& challengeNonce, const QString& clientNodeId);
    static bool verifyChallengeResponse(const QByteArray& verifier, const QByteArray& challengeNonce, const QString& clientNodeId, const QByteArray& response);

    // Authenticated Encryption (AEAD: Stream cipher + Encrypt-then-MAC HMAC-SHA256)
    static QJsonObject sealJson(const QJsonObject& plain, const QByteArray& transportKey, quint64 sequence);
    static bool openJson(const QJsonObject& envelope, const QByteArray& transportKey, QJsonObject* plainOut, quint64* sequenceOut = nullptr);

    // Payload signing for raw messages when needed
    static QByteArray hmacSha256(const QByteArray& key, const QByteArray& message);
    static QByteArray randomSessionKey();

private:
    static QByteArray keystreamXor(const QByteArray& data, const QByteArray& encKey, const QByteArray& nonce);
};

} // namespace pandora
