#ifndef _BLOCK_HPP_
#define _BLOCK_HPP_

#include <cstdint>

/// All placeable/generatable voxel types. COUNT must stay last.
enum class BlockType : uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Cobblestone,
    OakLog,
    DarkOakLog,
    SpruceLog,
    OakPlanks,
    Bricks,
    Sand,
    Gravel,
    OakLeaves,
    Water,
    COUNT
};

/// The six cube faces used for meshing and per-face tile lookup.
enum class BlockFace : uint8_t {
    Top = 0,
    Bottom,
    North,
    South,
    East,
    West
};

/// Tile indices into the atlas (must match TextureAtlas build order)
enum BlockTile : uint8_t {
    TILE_GRASS_TOP = 0,
    TILE_GRASS_SIDE,
    TILE_DIRT,
    TILE_STONE,
    TILE_COBBLESTONE,
    TILE_OAK_LOG,
    TILE_OAK_LOG_TOP,
    TILE_DARK_OAK_LOG,
    TILE_DARK_OAK_TOP,
    TILE_SPRUCE_LOG,
    TILE_SPRUCE_LOG_TOP,
    TILE_OAK_PLANKS,
    TILE_BRICKS,
    TILE_SAND,
    TILE_GRAVEL,
    TILE_OAK_LEAVES,
    TILE_WATER,
    TILE_COUNT
};

/// Atlas tile for a block's given face, accounting for top/bottom variants
/// (e.g. grass top vs. side, log end-grain vs. bark).
inline uint8_t GetBlockTextureTile(BlockType type, BlockFace face) {
    switch (type) {
        case BlockType::Grass:
            if (face == BlockFace::Top)    return TILE_GRASS_TOP;
            if (face == BlockFace::Bottom) return TILE_DIRT;
            return TILE_GRASS_SIDE;
        case BlockType::Dirt:
            return TILE_DIRT;
        case BlockType::Stone:
            return TILE_STONE;
        case BlockType::Cobblestone:
            return TILE_COBBLESTONE;
        case BlockType::OakLog:
            if (face == BlockFace::Top || face == BlockFace::Bottom) return TILE_OAK_LOG_TOP;
            return TILE_OAK_LOG;
        case BlockType::DarkOakLog:
            if (face == BlockFace::Top || face == BlockFace::Bottom) return TILE_DARK_OAK_TOP;
            return TILE_DARK_OAK_LOG;
        case BlockType::SpruceLog:
            if (face == BlockFace::Top || face == BlockFace::Bottom) return TILE_SPRUCE_LOG_TOP;
            return TILE_SPRUCE_LOG;
        case BlockType::OakPlanks:
            return TILE_OAK_PLANKS;
        case BlockType::Bricks:
            return TILE_BRICKS;
        case BlockType::Sand:
            return TILE_SAND;
        case BlockType::Gravel:
            return TILE_GRAVEL;
        case BlockType::OakLeaves:
            return TILE_OAK_LEAVES;
        case BlockType::Water:
            return TILE_WATER;
        default:
            return TILE_GRASS_TOP;
    }
}

/// Whether the block participates in collision (Air and Water do not).
inline bool IsBlockSolid(BlockType type) {
    return type != BlockType::Air && type != BlockType::Water;
}

/// Whether the block fully occludes neighboring faces for mesh culling and
/// occlusion. Leaves and water are excluded so light and visibility still
/// pass through them.
inline bool IsBlockOpaque(BlockType type) {
    return type != BlockType::Air && type != BlockType::OakLeaves && type != BlockType::Water;
}

/// Whether the block should be meshed at all (excludes Air).
inline bool IsBlockRenderable(BlockType type) {
    return type != BlockType::Air;
}

/// Whether the block is water, meshed and rendered on its own pass.
inline bool IsBlockWater(BlockType type) {
    return type == BlockType::Water;
}

/// Display name shown in the HUD.
inline const char* GetBlockName(BlockType type) {
    switch (type) {
        case BlockType::Grass:       return "Grass";
        case BlockType::Dirt:        return "Dirt";
        case BlockType::Stone:       return "Stone";
        case BlockType::Cobblestone: return "Cobblestone";
        case BlockType::OakLog:      return "Oak Log";
        case BlockType::DarkOakLog:  return "Dark Oak Log";
        case BlockType::SpruceLog:   return "Spruce Log";
        case BlockType::OakPlanks:   return "Oak Planks";
        case BlockType::Bricks:      return "Bricks";
        case BlockType::Sand:        return "Sand";
        case BlockType::Gravel:      return "Gravel";
        case BlockType::OakLeaves:   return "Oak Leaves";
        case BlockType::Water:       return "Water";
        case BlockType::Air:         return "Air";
        default:                     return "Unknown";
    }
}

#endif
