#ifndef _WORLD_META_HPP_
#define _WORLD_META_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "WorldClock.hpp"

/// Player position/orientation/settings persisted in world.dat.
struct PlayerState {
    float posX = 8.0f, posY = 70.0f, posZ = 8.0f;
    float pitch = 0.0f, yaw = 0.0f;
    uint8_t selectedBlock = 1; // BlockType::Grass
    int32_t renderDistance = 8;
};

/// world.dat schema: world identity, player state, world clock (version 2)
/// and the region index. A field change bumps CURRENT_VERSION and adds a
/// legacy path to SaveManager::ReadWorldDat; MIN_VERSION stays loadable.
/// @ingroup persistence
struct WorldMeta {
    static constexpr uint32_t MAGIC = 0x534C4B57; // "SLKW"
    static constexpr uint16_t CURRENT_VERSION = 2;
    /// Oldest world.dat version ReadWorldDat still loads.
    static constexpr uint16_t MIN_VERSION = 1;
    /// First version that stores the world clock.
    static constexpr uint16_t CLOCK_VERSION = 2;

    uint16_t version = CURRENT_VERSION;
    uint16_t flags = 0;
    int64_t saveTimestamp = 0;
    std::string worldName = "Default";
    uint32_t seed = 0;
    PlayerState player;
    WorldClockState clock;  // noon when loaded from version 1

    struct RegionEntry {
        int32_t rx, rz;
        int32_t chunkCount;
    };
    std::vector<RegionEntry> regions;
};

#endif
