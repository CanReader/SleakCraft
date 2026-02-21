#ifndef _BLOCK_HPP_
#define _BLOCK_HPP_

#include <cstdint>

enum class BlockType : uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    COUNT
};

enum class BlockFace : uint8_t {
    Top = 0,
    Bottom,
    North,
    South,
    East,
    West
};

// Returns the texture atlas tile index for a given block type and face
inline uint8_t GetBlockTextureTile(BlockType type, BlockFace face) {
    switch (type) {
        case BlockType::Grass:
            if (face == BlockFace::Top)    return 0; // grass_top
            if (face == BlockFace::Bottom) return 2; // dirt
            return 1; // grass_side
        case BlockType::Dirt:
            return 2;
        case BlockType::Stone:
            return 3;
        default:
            return 0;
    }
}

inline bool IsBlockSolid(BlockType type) {
    return type != BlockType::Air;
}

inline const char* GetBlockName(BlockType type) {
    switch (type) {
        case BlockType::Grass: return "Grass";
        case BlockType::Dirt:  return "Dirt";
        case BlockType::Stone: return "Stone";
        case BlockType::Air:   return "Air";
        default:               return "Unknown";
    }
}

#endif
