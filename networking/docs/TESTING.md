# Testing

The CMake test executable uses Qt Test with QCoreApplication, not GUI widgets.

## Automated coverage

- protocol_tests: all fourteen type names, Unicode JSON round trips, invalid envelopes, oversized outbound frame, oversized inbound length, JSON array/malformed JSON rejection, byte-by-byte fragmentation, coalesced messages.
- transport: three loopback peers, HELLO completion, direct delivery, broadcast, local sender spoof rejection, disconnect notifications and stop cleanup.
- simultaneousDial: both peers dial together, converge to one connection, then deliver a message.
- mismatchAndTimeout: reject mismatched session IDs and expire a silent TCP handshake.
- discovery: real UDP receive, wrong-session filtering, candidate endpoint extraction, discovery stop.
- limitsAndRestart: single-node capacity, invalid repeated start, idempotent stop, restart, missing-peer send and empty broadcast.
- simultaneousDialAtCapacity: maxNodes=2 with both sides dialing, bidirectional delivery, and rejection of a third distinct ready peer.
- handshakeCapacityBound: two pending sockets allowed with maxNodes=2; excess inbound/outbound sockets rejected without increasing ready membership.
- framingValidPrefix: four malformed suffix types, every two-part byte split, exactly two valid prefix objects and no post-error object.
- validPrefixDelivery: real TCP delivery for combined and deliberately separate writes, testing malformed JSON, zero length, oversized length and a JSON array. Exactly two preceding messages are received before disconnect; a valid message after the error is not received.

Run protocol-only checks:
```sh
ctest --test-dir build -L unit --output-on-failure
```

Run socket checks:
```sh
ctest --test-dir build -L integration --output-on-failure
```

Run the complete suite:
```sh
ctest --test-dir build --output-on-failure
```

Socket tests are not silently skipped on restricted machines. CTest timeouts are failures. Tests produce individual *-results.txt logs inside build. Read them when CTest reports failure, or execute network_tests with -v2 for per-assertion detail. Qt Test's init/cleanup cases are counted separately from test functions.

## Latest executed verification — 2026-10-26

Toolchain: Windows, Qt 6.11.2, GCC 13.1, CMake 4.4.3, Qt-bundled Ninja. No packages installed.

First, with only the regressions added and the old transport unchanged:

```powershell
$env:PATH="C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.2\mingw_64\bin;"+$env:PATH
cmake -S . -B build-audit-fixes -G Ninja -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
cmake --build build-audit-fixes
$env:QT_LOGGING_RULES="pandora.networking.diagnostics.info=false"
.\build-audit-fixes\network_tests.exe simultaneousDialAtCapacity validPrefixDelivery -o -,txt
```

Configure/build succeeded. The regression command exited 1: **6 passed, 5 failed** including Qt initialization/cleanup. The capacity regression failed, all four combined-input delivery rows failed, and all four split-input rows passed.

After applying the capacity and receive-loop fixes:

```powershell
cmake --build build-audit-fixes
ctest --test-dir build-audit-fixes --output-on-failure -j 5
```

Both commands exited 0. Build succeeded with no warnings reported. CTest: **9/9 passed**, 0 failures, 1.07 seconds elapsed on the final run (the first post-fix run also passed, in 1.10 seconds). The protocol entry includes the four framingValidPrefix rows. The validPrefixDelivery entry includes eight TCP rows. Other entries cover the existing tests plus the two capacity regressions.

Historical logs under build, build-diagnostics and docs/verification were neither deleted nor overwritten. This run's logs are under build-audit-fixes and ignored by Git.

## Manual localhost verification (user reported)

Before these fixes, Alice and Bob ran as separate processes on one Windows computer. The user confirmed UDP discovery, TCP connection, HELLO acceptance, PING/PONG and CREATE_PANDORA application messages in both directions. The console example already sends that demo on peerConnected and prints messageReceived. This manual result is distinct from the automated verification above; it is not a two-computer LAN result.

## Historical execution-environment discrepancy

The legacy C:\MinGW compiler cannot launch. The matching Qt MinGW compiler builds the project.

Earlier runs had four socket-related CTest failures. Diagnostic TCP logs showed errorCode=3 / Permission denied before any HELLO. An independent .NET TcpClient-to-TcpListener attempt also reported socket access permission denial. UDP receive yielded no advertisement. These failures were recorded, not silently skipped.

During the later read-only audit, the old and diagnostic executables each passed all nine original test functions (11 Qt Test cases including initialization/cleanup; exit 0). The independent .NET loopback probe passed as well. Thus the earlier condition is not currently reproducible; the exact external restriction was not identified, and diagnostics alone are not established as its cause. No permission or firewall settings were altered. Current fixed-source results are recorded above; all historical results remain in PROGRESS.md.

Still unverified: two-computer broadcast/TCP behavior, physical Wi-Fi loss, Linux/macOS builds, sustained heartbeat timeout after a ready connection, output queue saturation and multiple concurrent same-direction dials. Authentication, full-mesh orchestration and reliable application ACK/replay remain outside Member 1.

## Two-laptop acceptance checks (not run here)

1. Use the same Wi-Fi/LAN with client isolation disabled by its operator. Check intended TCP and UDP ports are allowed by each host's normal firewall policy.
2. Launch the console example on both machines with one shared public session ID and distinct temporary IDs.
3. Confirm each logs peerDiscovered; discovery is a hint and does not dial automatically.
4. Dial one actual LAN endpoint from the second machine. Confirm both log peerConnected and receive the example envelope.
5. Stop one process; confirm peerDisconnected at the other. Repeat by removing Wi-Fi and wait for configured timeout.
6. Restart and manually reconnect. Confirm messaging resumes without old peers in the snapshot.
7. Start two peers that dial each other simultaneously and confirm one connection per peer.
8. Use a different session ID and confirm no discovery candidate/membership connection is accepted.
9. Test on Windows, Linux, and macOS before claiming cross-platform verification.

Further integration checks belong to the application team: key authentication, event ACK/replay, file integrity, storage healing, metadata convergence and Guardian authorization.



