# Pandora Wire Protocol Specification (v1.0)

## Transport & Ports
- **UDP Discovery Port**: `43171` (IPv4 broadcast `255.255.255.255`, subnet broadcasts, and `127.0.0.1`).
- **TCP Mesh Port**: `43170` default (configurable per node, with automatic ephemeral port fallback if busy).
- **Framing**: Newline-delimited compact JSON (`\n`), capped at `8 MiB` (`PeerNode::kMaxFrameBytes`) per frame to prevent memory exhaustion.

## Cryptographic Handshake & AEAD Envelope
1. **Discovery (`UDP`)**:
   ```json
   {
     "kind": "discovery",
     "pandoraId": "<session-uuid>",
     "name": "My Pandora",
     "hostNodeId": "<node-uuid>",
     "hostName": "Neytiri",
     "port": 43170,
     "nodeCount": 3
   }
   ```
2. **Challenge-Response Join (`TCP`)**:
   - `join_request`: Sent by joining peer with `nodeId`, `name`, `listenPort`, `quotaBytes`, `availableDiskBytes`.
   - `join_challenge`: Host replies with session `salt` (32 bytes hex) and single-use `nonce` (24 bytes hex).
   - `join_response`: Client derives `masterKey = PBKDF2-HMAC-SHA256(key, salt, 60000)` and `verifier = HMAC(masterKey, "pandora-auth-verifier-v1")`, then sends `response = HMAC(verifier, nonce + ":" + clientNodeId)`.
   - `join_accept` / `join_reject`: Host verifies `response` in constant time. If valid, sends full metadata snapshot and marks socket authenticated.
3. **Authenticated Encryption (`sealed`)**:
   - Once authenticated, every TCP message is encrypted and MAC-authenticated using `transportKey = HMAC(masterKey, "pandora-transport-key-v1:" + pandoraId)`:
   ```json
   {
     "kind": "sealed",
     "seq": "42",
     "nonce": "<base64-24B>",
     "ct": "<base64-ciphertext>",
     "tag": "<base64-hmac-sha256>"
   }
   ```
   - Receivers verify the HMAC-SHA256 tag in constant time and enforce strictly increasing `seq` numbers per connection to reject replays.

## Data & Control Messages (Inside `sealed` Payload)
- `peer_hello`: Direct peer-to-peer authentication handshake for full mesh connectivity.
- `ping` / `pong`: 3-second heartbeat probes; peers silent for >12 seconds are disconnected and trigger replica repair / Guardian election.
- `event`: Propagates deduplicated `PandoraEvent` envelopes (`NodeJoined`, `NodeLeft`, `NodeUpdated`, `GuardianChanged`, `FileAdded`, `FileUpdated`, `FileDeleted`, `ReplicaChanged`, `ShutdownWarning`, `ShutdownCancelled`, `SessionDeactivated`).
- `snapshot` / `snapshot_request`: Full state anti-entropy synchronization.
- `replica_push_begin` / `replica_push_chunk` / `replica_chunk_ack` / `replica_push_end` / `replica_commit_ack` / `replica_push_nack`: Chunked replica distribution with end-to-end SHA-256 verification.
- `file_pull_request` / `file_pull_chunk` / `file_pull_error`: Chunked remote file retrieval and replica fallback.
