#pragma once
/**
 * @file Constants.h
 * @brief Project-wide constants shared by all Pandora modules.
 *
 * SHARED CONTRACT - identical in all four modules. Do not edit in one module only.
 */

namespace pandora::shared::constants {

/// Version of the wire protocol; bump on incompatible Message/payload changes.
inline constexpr int kProtocolVersion = 1;

/// Default maximum number of Na'vi in one Pandora (configurable per Pandora).
inline constexpr int kDefaultMaxNavi = 20;

/// Upper bound for the replication factor: factor = min(kMaxReplicationFactor, connected Na'vi).
inline constexpr int kMaxReplicationFactor = 3;

/// Seconds between the shutdown warning and the actual deactivation.
inline constexpr int kShutdownGraceSeconds = 120;

}  // namespace pandora::shared::constants
