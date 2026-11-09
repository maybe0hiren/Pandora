# Initial wire protocol (prototype)

Transport:
- UDP broadcast discovery on port 43171.
- TCP control messages on port 43170.
- One compact JSON object per line.

Message kinds:
- `discovery`: `pandoraId`, `port`, `name`.
- `snapshot`: `data` with metadata snapshot.
- `event`: serialized event envelope.

This is not a production protocol. It lacks authentication, encryption, message size caps, connection initiation in both directions, backpressure, acknowledgements, retransmission, schema version negotiation, and safe handling of untrusted peers. Never expose these ports to the public internet.
