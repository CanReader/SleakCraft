#ifndef _WORLD_META_HPP_
#define _WORLD_META_HPP_

#include <cstdint>
#include <string>
#include <vector>

struct PlayerState {
    float posX = 8.0f, posY = 6.62f, posZ = 8.0f;
    float pitch = 0.0f, yaw = 0.0f;
    uint8_t selectedBlock = 1; // BlockType::Grass
    int32_t renderDistance = 8;
};

struct WorldMeta {
    static constexpr uint32_t MAGIC = 0x534C4B57; // "SLKW"
    static constexpr uint16_t CURRENT_VERSION = 1;

    uint16_t version = CURRENT_VERSION;
    uint16_t flags = 0;
    int64_t saveTimestamp = 0;
    std::string worldName = "Default";
    uint32_t seed = 0;
    PlayerState player;

    struct RegionEntry {
        int32_t rx, rz;
        int32_t chunkCount;
    };
    std::vector<RegionEntry> regions;
};

#endif
