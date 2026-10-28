# Architecture and integration

All peers run the same NetworkManager. Each manager has a TCP listener and outgoing connections. A listener is a role of every peer, not a permanent server. UDP discovery provides endpoint hints; an external application coordinator builds the desired mesh.

```mermaid
classDiagram
 class NetworkManager {
  +start()
  +stop()
  +connectToPeer()
  +disconnectPeer()
  +sendMessage()
  +broadcastMessage()
  +getConnectedPeers()
  +startDiscovery()
  +stopDiscovery()
 }
 class FrameDecoder {
  +feed()
 }
 class Message {
  +toJson()
  +fromJson()
 }
 class Node
 NetworkManager *-- FrameDecoder : per connection
 NetworkManager --> Message : transports
 NetworkManager --> Node : snapshots
```

NetworkManager owns TCP/UDP sockets and timers through Qt QObject lifetime management. Private per-stream state is held with unique_ptr. Protocol and model declarations have no dependency on UI, SQL, storage, or authentication implementations. Public APIs are in shared include headers; callers receive values and signals.

## Connect and message sequence

```mermaid
sequenceDiagram
 participant App as Application coordinator
 participant A as Peer A transport
 participant B as Peer B transport
 participant Consumer as Member 2/3/4
 App->>A: connectToPeer(endpoint)
 A->>B: TCP connect
 A->>B: framed HELLO
 B->>A: framed HELLO
 A-->>App: peerConnected(candidate)
 B-->>Consumer: peerConnected(candidate)
 Note over App,Consumer: Application performs authentication and admission
 App->>A: sendMessage(peer, envelope)
 A->>B: length + JSON
 B-->>Consumer: messageReceived(peer, envelope)
```

## Disconnect sequence

```mermaid
sequenceDiagram
 participant A as Peer A transport
 participant B as Peer B transport
 participant Coordinator as Local coordinator
 A->>B: PING
 Note over B: Process or network unavailable
 Note over A: Last valid frame exceeds timeout
 A-->>Coordinator: networkError(timeout)
 A-->>Coordinator: peerDisconnected(nodeId)
 Note over Coordinator: Metadata, healing and election belong to Members 2/3
```

No NAVI_JOINED/NAVI_LEFT event is manufactured from socket state. The application coordinator determines membership and broadcasts those events after authentication. This separation prevents the transport from silently implementing metadata or lifecycle policy.

