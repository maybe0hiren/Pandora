# Public API contract

All public declarations are under include/pandora. Include <pandora/networking/NetworkManager.h> and link Pandora::Networking. Node, Pandora and FileMetadata are shared value DTOs; only Message has a wire serializer. Keep changes to these shared contracts coordinated with all four members.

## Ownership and threading

NetworkManager is a final QObject with normal Qt parent ownership. It owns listeners, timers and TCP sockets. Sockets use QObject ownership and deleteLater; per-connection state uses unique_ptr. There are no owning raw pointers exposed publicly.

A running QCoreApplication and event loop are required. Construct, use, stop, and destroy a manager on its QObject thread. APIs are event-loop confined, not lock-based thread safe. Cross-thread callers use QMetaObject::invokeMethod(manager, lambda, Qt::QueuedConnection); obtain snapshots in that lambda and return them through queued signals. Node and Message are registered Qt metatypes for queued signal delivery. Move a stopped manager only; its child network objects and timers follow.

Do not delete a manager synchronously inside its signals; use deleteLater. Slots may call stop or disconnectPeer. Keep direct slots fast so networking and heartbeats can run.

## NetworkConfig

| Field | Default | Meaning |
|---|---|---|
| maxNodes | 20 | Self plus distinct ready transport peers; range 1..1000 |
| heartbeatMs | 2000 | Timer and PING interval; minimum 10 ms |
| timeoutMs | 10000 | Handshake / last valid frame timeout; >=2 heartbeat intervals |
| maxQueuedBytes | 4194304 | Per-socket Qt output-buffer bound; >=1048580 |

The default allows 19 distinct ready remote peers. To resolve simultaneous opposite-direction dials, total tracked sockets (ready plus pending) are bounded at 2 * (maxNodes - 1). Outgoing dials additionally require ready peers plus pending outgoing dials to remain below maxNodes - 1. Incoming handshakes use the bounded spare sockets; after HELLO, duplicates are resolved before the ready-peer limit is enforced. Thus spare handshake capacity cannot admit extra distinct ready peers. maxNodes is a local transport resource limit, not a distributed admission or authentication guarantee.

## Methods

| API | Contract / result |
|---|---|
| NetworkManager(parent) | Registers metatypes and wires internal listeners. Does not start networking. |
| start(local, pandoraId, bindAddress, port, config) | Returns false on invalid config, empty/oversize IDs, or listen failure. Cannot start twice. Node ID, name and session ID are capped at 128 UTF-16 code units. Port 0 chooses a port. local.address/port are not used as bind options. |
| listeningPort() | Actual TCP listening port; zero when stopped. |
| stop() | Idempotently stops discovery/listener/timers, drops pending and ready connections, clears local session state. Emits disconnection for each ready peer. |
| connectToPeer(address, port) | Returns true if an asynchronous dial was started. False for stopped manager, null address, port zero, or capacity. peerConnected reports completed HELLO. Failed attempts emit networkError. |
| disconnectPeer(nodeId) | Immediate local close. True if a ready peer existed, false otherwise. No NAVI_LEFT event is fabricated. |
| sendMessage(nodeId, message) | Validates envelope and local sender/session, then queues one frame. False for missing peer, invalid/oversize message, output backpressure, or socket failure. The caller retains the Message. True means accepted by the socket output buffer only. |
| broadcastMessage(message) | Attempts direct send to a snapshot of ready peers; returns successful enqueue count. May partially succeed. Zero also represents no peers. |
| getConnectedPeers() | Copies currently handshaken transport peer DTOs, excluding self/pending connections; ordering unspecified. It is not an authentication roster. |
| startDiscovery(port, intervalMs) | Starts/restarts IPv4 UDP discovery on a shared port. Requires active listener, nonzero UDP port and interval >=100 ms. Returns false on invalid input/bind failure. Advertises immediately and periodically. Datagram send errors use networkError. |
| stopDiscovery() | Stops UDP and its timer only. Does not disconnect TCP peers. |

## Signals

| Signal | Meaning |
|---|---|
| peerDiscovered(Node) | Valid same-session advertisement candidate. Repeats on later beacons. Address comes from the datagram, not JSON. No automatic dial or authentication. |
| peerConnected(Node) | HELLO complete on a TCP stream. Identity is self-asserted, not verified. |
| peerDisconnected(QString) | Ready transport connection removed (explicit close, remote close, invalid frame, timeout, or duplicate replacement). Pending failures do not have a peer ID. |
| messageReceived(QString, Message) | Structurally valid application envelope whose sender/session matches HELLO. It is still untrusted application input. |
| networkError(QString) | Diagnostic only. No file logger or durable history. Caller decides how to display/retain it. |

Discovery repeats are deliberate: applications can maintain expiry and retry policy without this transport pretending that a beacon constitutes membership. TCP heartbeat/close signals detect connected peer departure. Timeout may be delayed by a blocked event loop.

## Minimal application wiring

See examples/peer.cpp. Connect messageReceived to an application coordinator that validates admission through Member 4 and dispatches events to Members 2/3. These consumers must never access NetworkManager's private members. All local lifecycle and metadata mutations remain outside this library.



