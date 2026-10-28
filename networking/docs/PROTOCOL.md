# Pandora transport protocol v1

## TCP framing

Every message is a 4-byte unsigned big-endian length followed by exactly that many UTF-8 bytes encoding one compact JSON object. Length must be 1..1048576 bytes. No newline delimiter, native byte order, QDataStream dependency, compression, or raw binary stream is used. Split reads and multiple frames per read are supported. Complete valid frames preceding malformed framing/JSON are processed in order before the connection is closed; malformed data and any suffix after it are not delivered. This valid-prefix behavior is independent of TCP read segmentation.

Each socket has a bounded Qt read buffer, an incremental decoder and a bounded Qt write queue. Data is read in 16 KiB slices. The decoder consumes input in 4 KiB slices. A slow sender must finish its HELLO before timeout; partial frames never refresh liveness.

## Transport controls

First frame in each direction must be:

```json
{"control":"HELLO","protocol":1,"pandoraId":"session-uuid","nodeId":"node-uuid","name":"Alice","port":45455}
```

port is the peer's listening TCP port (1..65535), not its outgoing source port. The observed socket address is authoritative. Self-connections, session mismatch, incompatible versions, empty/oversize identifiers and malformed HELLO are rejected. Node UUID validation and proof of ownership belong to Member 4. A session ID is a public discovery namespace, not a credential.

After HELLO, {"control":"PING"} is answered with {"control":"PONG"}. Valid application messages also refresh lastSeen. Unknown controls and repeated HELLO close the stream. Missing heartbeat responses or unfinished handshakes cause removal after timeout (checked every heartbeat interval).

For simultaneous opposite-direction connections, both peers prefer the stream initiated by the lexically smaller node ID. Already-established duplicates in the preferred direction are rejected. Replacement may emit disconnect/connect; application membership code should debounce transport transitions as appropriate.

## Application envelope

```json
{"protocol":1,"type":"FILE_REQUEST","id":"message-uuid","pandoraId":"session-uuid","senderId":"node-uuid","payload":{"fileId":"example"}}
```

Required: exact numeric protocol version, supported case-sensitive type, nonempty string id/senderId/pandoraId, object payload. Extra fields are ignored. Envelope IDs default to random UUIDs. Sender and session must match the connection. Message IDs are not automatically deduplicated.

Supported types:
CREATE_PANDORA, JOIN_REQUEST, JOIN_ACCEPTED, NAVI_JOINED, NAVI_LEFT, FILE_UPLOAD, FILE_UPDATE, FILE_DELETE, FILE_REQUEST, FILE_RESPONSE, METADATA_SYNC, GUARDIAN_CHANGED, SHUTDOWN_WARNING, PANDORA_DEACTIVATE.

Payload schemas are intentionally owned by Members 2-4. The networking module does not assert versions, choose replicas, execute deletion/shutdown, validate keys, or authorize Guardians. Keep transport controls separate from application types. Application payloads must remain within the total serialized frame limit.

## UDP discovery

IPv4 JSON datagrams, without a TCP length prefix:

```json
{"service":"pandora-discovery","protocol":1,"pandoraId":"session-uuid","nodeId":"node-uuid","name":"Alice","port":45455}
```

Default port 45454; configurable. Advertisements go to limited broadcast and active IPv4 interface broadcast addresses. Receivers accept <=1024-byte JSON objects with matching service/version/session and valid node/name/port, ignore self, and emit peerDiscovered with the observed source address. No key, verifier, or file metadata is advertised. Discovery never connects automatically and makes no authentication assertion. IPv6 UDP discovery is not implemented; explicit TCP addresses can be IPv6 when a suitable listener is selected.

## Delivery and failure semantics

TCP guarantees in-order bytes while a connection remains viable. The transport does not acknowledge application processing. A disconnect can leave the sender uncertain whether the last event was processed. Consumers must use event IDs, idempotency, application ACKs and synchronization to handle retries. No durable outbox, reconnection, replay, or exactly-once guarantee exists here. Backpressure rejects new sends; callers retry deliberately. Broadcast covers direct neighbors only; a full mesh is the integration controller's responsibility.

Untrusted peers can assert another UUID or emit application events. Add Member 4's authentication/admission layer before treating these as authorized. Plain QTcpSocket does not encrypt data. This module deliberately does not implement a fake encryption or authentication abstraction.



