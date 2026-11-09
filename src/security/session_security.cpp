#include "security/session_security.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QtEndian>

namespace pandora {

QByteArray SessionSecurity::createSalt(int bytes) {
    QByteArray salt(bytes, Qt::Uninitialized);
    for (qsizetype i = 0; i < salt.size(); i += 4) {
        quint32 r = QRandomGenerator::system()->generate();
        for (int b = 0; b < 4 && i + b < salt.size(); ++b) {
            salt[i + b] = static_cast<char>((r >> (b * 8)) & 0xff);
        }
    }
    return salt;
}

QByteArray SessionSecurity::createNonce() {
    return createSalt(kNonceBytes);
}

QByteArray SessionSecurity::hmacSha256(const QByteArray& key, const QByteArray& message) {
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

QByteArray SessionSecurity::deriveMasterKey(const QString& passkey, const QByteArray& salt, int iterations) {
    // Standard PBKDF2-HMAC-SHA256 (RFC 2898 / RFC 8018) producing 32-byte (256-bit) key
    const QByteArray passwordBytes = passkey.toUtf8();
    QByteArray saltWithBlock = salt;
    char blockIndex[4] = {0, 0, 0, 1};
    saltWithBlock.append(blockIndex, 4);

    QByteArray u = hmacSha256(passwordBytes, saltWithBlock);
    QByteArray result = u;
    const int count = qMax(1000, iterations);
    for (int i = 1; i < count; ++i) {
        u = hmacSha256(passwordBytes, u);
        for (qsizetype j = 0; j < result.size(); ++j) {
            result[j] = static_cast<char>(result[j] ^ u[j]);
        }
    }
    return result;
}

QByteArray SessionSecurity::deriveVerifier(const QString& passkey, const QByteArray& salt) {
    const QByteArray master = deriveMasterKey(passkey, salt);
    return hmacSha256(master, QByteArrayLiteral("pandora-auth-verifier-v1"));
}

QByteArray SessionSecurity::deriveTransportKey(const QByteArray& masterKey, const QString& pandoraId) {
    return hmacSha256(masterKey, QByteArrayLiteral("pandora-transport-key-v1:") + pandoraId.toUtf8());
}

bool SessionSecurity::constantTimeEquals(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size() || a.isEmpty()) return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool SessionSecurity::verify(const QString& passkey, const QByteArray& salt, const QByteArray& verifier) {
    return constantTimeEquals(deriveVerifier(passkey, salt), verifier);
}

QByteArray SessionSecurity::computeChallengeResponse(const QByteArray& verifier, const QByteArray& challengeNonce, const QString& clientNodeId) {
    QByteArray msg = challengeNonce + ":" + clientNodeId.toUtf8();
    return hmacSha256(verifier, msg);
}

bool SessionSecurity::verifyChallengeResponse(const QByteArray& verifier, const QByteArray& challengeNonce, const QString& clientNodeId, const QByteArray& response) {
    const QByteArray expected = computeChallengeResponse(verifier, challengeNonce, clientNodeId);
    return constantTimeEquals(expected, response);
}

QByteArray SessionSecurity::keystreamXor(const QByteArray& data, const QByteArray& encKey, const QByteArray& nonce) {
    QByteArray out(data.size(), Qt::Uninitialized);
    quint64 counter = 0;
    qsizetype offset = 0;
    while (offset < data.size()) {
        char ctrBytes[8];
        qToBigEndian(counter, reinterpret_cast<uchar*>(ctrBytes));
        QByteArray blockInput = nonce + QByteArray(ctrBytes, 8);
        const QByteArray keystreamBlock = hmacSha256(encKey, blockInput);
        const qsizetype chunk = qMin<qsizetype>(keystreamBlock.size(), data.size() - offset);
        for (qsizetype i = 0; i < chunk; ++i) {
            out[offset + i] = static_cast<char>(data[offset + i] ^ keystreamBlock[i]);
        }
        offset += chunk;
        ++counter;
    }
    return out;
}

QJsonObject SessionSecurity::sealJson(const QJsonObject& plain, const QByteArray& transportKey, quint64 sequence) {
    const QByteArray encKey = hmacSha256(transportKey, QByteArrayLiteral("pandora-enc-subkey"));
    const QByteArray macKey = hmacSha256(transportKey, QByteArrayLiteral("pandora-mac-subkey"));
    const QByteArray nonce = createNonce();
    const QByteArray plainBytes = QJsonDocument(plain).toJson(QJsonDocument::Compact);
    const QByteArray cipherBytes = keystreamXor(plainBytes, encKey, nonce);

    const QString seqStr = QString::number(sequence);
    QByteArray aad = nonce + ":" + seqStr.toUtf8() + ":" + cipherBytes;
    const QByteArray tag = hmacSha256(macKey, aad);

    return {
        {"kind", "sealed"},
        {"seq", seqStr},
        {"nonce", QString::fromLatin1(nonce.toBase64())},
        {"ct", QString::fromLatin1(cipherBytes.toBase64())},
        {"tag", QString::fromLatin1(tag.toBase64())}
    };
}

bool SessionSecurity::openJson(const QJsonObject& envelope, const QByteArray& transportKey, QJsonObject* plainOut, quint64* sequenceOut) {
    if (envelope.value("kind").toString() != "sealed") return false;
    const QString seqStr = envelope.value("seq").toString();
    const QByteArray nonce = QByteArray::fromBase64(envelope.value("nonce").toString().toLatin1());
    const QByteArray cipherBytes = QByteArray::fromBase64(envelope.value("ct").toString().toLatin1());
    const QByteArray tag = QByteArray::fromBase64(envelope.value("tag").toString().toLatin1());

    if (nonce.size() != kNonceBytes || tag.size() != 32) return false;

    const QByteArray encKey = hmacSha256(transportKey, QByteArrayLiteral("pandora-enc-subkey"));
    const QByteArray macKey = hmacSha256(transportKey, QByteArrayLiteral("pandora-mac-subkey"));

    QByteArray aad = nonce + ":" + seqStr.toUtf8() + ":" + cipherBytes;
    const QByteArray expectedTag = hmacSha256(macKey, aad);
    if (!constantTimeEquals(tag, expectedTag)) return false;

    const QByteArray plainBytes = keystreamXor(cipherBytes, encKey, nonce);
    const QJsonDocument doc = QJsonDocument::fromJson(plainBytes);
    if (!doc.isObject()) return false;

    if (plainOut) *plainOut = doc.object();
    if (sequenceOut) *sequenceOut = seqStr.toULongLong();
    return true;
}

QByteArray SessionSecurity::randomSessionKey() {
    return createSalt(32).toHex();
}

} // namespace pandora
