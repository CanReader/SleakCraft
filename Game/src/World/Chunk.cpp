#include "World/Chunk.hpp"
#include "World/WorldGenerator.hpp"
#include <Core/GameObject.hpp>
#include <Core/SceneBase.hpp>
#include <ECS/Components/TransformComponent.hpp>
#include <ECS/Components/MeshComponent.hpp>
#include <ECS/Components/MaterialComponent.hpp>
#include <Math/Vector.hpp>
#include <Runtime/Material.hpp>

using namespace Sleak;
using namespace Sleak::Math;

namespace {

// Water surface above the block base in packed position units
constexpr int32_t WATER_SURFACE_UNITS =
    static_cast<int32_t>(WATER_SURFACE_HEIGHT * VOXEL_POS_UNITS);
static_assert(WATER_SURFACE_UNITS == WATER_SURFACE_HEIGHT * VOXEL_POS_UNITS);
// The packed Y holds the top face of the highest block
constexpr int32_t WORLD_TOP_UNITS =
    (WorldGenerator::MAX_CHUNK_Y + 1) * Chunk::SIZE * VOXEL_POS_UNITS;
static_assert(WORLD_TOP_UNITS < 1 << (VOXEL_Y_BITS - 1));

}  // namespace

Chunk::Chunk(int cx, int cy, int cz) : m_cx(cx), m_cy(cy), m_cz(cz) {
    memset(m_blocks, static_cast<uint8_t>(BlockType::Air), VOLUME);
}

Chunk::~Chunk() {
    if (m_gameObject && !m_addedToScene)
        delete m_gameObject;
}

void Chunk::SetBlock(int x, int y, int z, BlockType type) {
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE) return;
    m_blocks[BlockIndex(x, y, z)] = static_cast<uint8_t>(type);
}

BlockType Chunk::GetBlock(int x, int y, int z) const {
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE)
        return BlockType::Air;
    return static_cast<BlockType>(m_blocks[BlockIndex(x, y, z)]);
}

void Chunk::SetNeighbor(BlockFace face, Chunk* chunk) {
    m_neighbors[static_cast<uint8_t>(face)] = chunk;
}

bool Chunk::IsBlockSolidAt(int x, int y, int z) const {
    if (x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE)
        return IsBlockSolid(static_cast<BlockType>(m_blocks[BlockIndex(x, y, z)]));

    if (y >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Top)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y - SIZE, z));
        // No chunk above — at world ceiling, treat as air so top faces render
        return false;
    }
    if (y < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Bottom)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y + SIZE, z));
        return false;
    }
    if (z >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::North)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y, z - SIZE));
    }
    if (z < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::South)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y, z + SIZE));
    }
    if (x >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::East)];
        if (nb) return IsBlockSolid(nb->GetBlock(x - SIZE, y, z));
    }
    if (x < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::West)];
        if (nb) return IsBlockSolid(nb->GetBlock(x + SIZE, y, z));
    }

    return true;
}

bool Chunk::IsBlockOpaqueAt(int x, int y, int z) const {
    if (x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE)
        return IsBlockOpaque(static_cast<BlockType>(m_blocks[BlockIndex(x, y, z)]));

    if (y >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Top)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y - SIZE, z));
        return false;
    }
    if (y < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Bottom)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y + SIZE, z));
        // Under the world floor: the bottom layer needs no faces
        return m_cy <= WorldGenerator::MIN_CHUNK_Y;
    }
    if (z >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::North)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y, z - SIZE));
    }
    if (z < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::South)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y, z + SIZE));
    }
    if (x >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::East)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x - SIZE, y, z));
    }
    if (x < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::West)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x + SIZE, y, z));
    }

    return true;
}

void Chunk::GenerateMeshData() {
    VoxelVertexBuffer vertices;

    bool opaque[18][18][18];
    bool solid[18][18][18];
    for (int y = -1; y <= SIZE; ++y) {
        for (int z = -1; z <= SIZE; ++z) {
            for (int x = -1; x <= SIZE; ++x) {
                opaque[y+1][z+1][x+1] = IsBlockOpaqueAt(x, y, z);
                solid[y+1][z+1][x+1]  = IsBlockSolidAt(x, y, z);
            }
        }
    }

    // Occluder extraction: per-8x8-quadrant, ALL runs (>= 4 layers) of
    // fully-opaque horizontal layers, ascending. Keeping every run (not
    // just the longest) preserves the cave-to-surface shell that blocks
    // sightlines when the player is underground.
    int8_t occRunMinY[4][OCC_MAX_RUNS];
    int8_t occRunMaxY[4][OCC_MAX_RUNS];
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i < OCC_MAX_RUNS; ++i)
            occRunMinY[q][i] = occRunMaxY[q][i] = -1;
    {
        int runCount[4] = {0, 0, 0, 0};
        int curStart[4] = {-1, -1, -1, -1};
        auto closeRun = [&](int q, int endY) {
            int len = endY - curStart[q] + 1;
            if (len >= 4 && runCount[q] < OCC_MAX_RUNS) {
                occRunMinY[q][runCount[q]] = static_cast<int8_t>(curStart[q]);
                occRunMaxY[q][runCount[q]] = static_cast<int8_t>(endY);
                ++runCount[q];
            }
            curStart[q] = -1;
        };
        for (int y = 0; y < SIZE; ++y) {
            bool full[4] = {true, true, true, true};
            for (int z = 0; z < SIZE; ++z) {
                for (int x = 0; x < SIZE; ++x) {
                    if (!opaque[y+1][z+1][x+1])
                        full[(x >= 8) + 2 * (z >= 8)] = false;
                }
            }
            for (int q = 0; q < 4; ++q) {
                if (full[q]) {
                    if (curStart[q] < 0) curStart[q] = y;
                } else if (curStart[q] >= 0) {
                    closeRun(q, y - 1);
                }
            }
        }
        for (int q = 0; q < 4; ++q)
            if (curStart[q] >= 0) closeRun(q, SIZE - 1);
    }

    auto fastCalcAO = [](bool side1, bool side2, bool corner) {
        if (side1 && side2) return 0;
        return 3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner));
    };

    auto fastFaceAO = [&](BlockFace face, int x, int y, int z, uint32_t ao[4]) {
        auto isSolid = [&](int dx, int dy, int dz) {
            return solid[dy+1][dz+1][dx+1];
        };

        switch (face) {
            case BlockFace::Top: {
                int ay = y + 1;
                ao[0] = fastCalcAO(isSolid(x - 1, ay, z), isSolid(x, ay, z - 1),
                                   isSolid(x - 1, ay, z - 1));
                ao[1] = fastCalcAO(isSolid(x - 1, ay, z), isSolid(x, ay, z + 1),
                                   isSolid(x - 1, ay, z + 1));
                ao[2] = fastCalcAO(isSolid(x + 1, ay, z), isSolid(x, ay, z + 1),
                                   isSolid(x + 1, ay, z + 1));
                ao[3] = fastCalcAO(isSolid(x + 1, ay, z), isSolid(x, ay, z - 1),
                                   isSolid(x + 1, ay, z - 1));
                break;
            }
            case BlockFace::Bottom: {
                int ay = y - 1;
                ao[0] = fastCalcAO(isSolid(x - 1, ay, z), isSolid(x, ay, z + 1),
                                   isSolid(x - 1, ay, z + 1));
                ao[1] = fastCalcAO(isSolid(x - 1, ay, z), isSolid(x, ay, z - 1),
                                   isSolid(x - 1, ay, z - 1));
                ao[2] = fastCalcAO(isSolid(x + 1, ay, z), isSolid(x, ay, z - 1),
                                   isSolid(x + 1, ay, z - 1));
                ao[3] = fastCalcAO(isSolid(x + 1, ay, z), isSolid(x, ay, z + 1),
                                   isSolid(x + 1, ay, z + 1));
                break;
            }
            case BlockFace::North: {
                int az = z + 1;
                ao[0] = fastCalcAO(isSolid(x + 1, y, az), isSolid(x, y - 1, az),
                                   isSolid(x + 1, y - 1, az));
                ao[1] = fastCalcAO(isSolid(x + 1, y, az), isSolid(x, y + 1, az),
                                   isSolid(x + 1, y + 1, az));
                ao[2] = fastCalcAO(isSolid(x - 1, y, az), isSolid(x, y + 1, az),
                                   isSolid(x - 1, y + 1, az));
                ao[3] = fastCalcAO(isSolid(x - 1, y, az), isSolid(x, y - 1, az),
                                   isSolid(x - 1, y - 1, az));
                break;
            }
            case BlockFace::South: {
                int az = z - 1;
                ao[0] = fastCalcAO(isSolid(x - 1, y, az), isSolid(x, y - 1, az),
                                   isSolid(x - 1, y - 1, az));
                ao[1] = fastCalcAO(isSolid(x - 1, y, az), isSolid(x, y + 1, az),
                                   isSolid(x - 1, y + 1, az));
                ao[2] = fastCalcAO(isSolid(x + 1, y, az), isSolid(x, y + 1, az),
                                   isSolid(x + 1, y + 1, az));
                ao[3] = fastCalcAO(isSolid(x + 1, y, az), isSolid(x, y - 1, az),
                                   isSolid(x + 1, y - 1, az));
                break;
            }
            case BlockFace::East: {
                int ax = x + 1;
                ao[0] = fastCalcAO(isSolid(ax, y, z - 1), isSolid(ax, y - 1, z),
                                   isSolid(ax, y - 1, z - 1));
                ao[1] = fastCalcAO(isSolid(ax, y, z - 1), isSolid(ax, y + 1, z),
                                   isSolid(ax, y + 1, z - 1));
                ao[2] = fastCalcAO(isSolid(ax, y, z + 1), isSolid(ax, y + 1, z),
                                   isSolid(ax, y + 1, z + 1));
                ao[3] = fastCalcAO(isSolid(ax, y, z + 1), isSolid(ax, y - 1, z),
                                   isSolid(ax, y - 1, z + 1));
                break;
            }
            case BlockFace::West: {
                int ax = x - 1;
                ao[0] = fastCalcAO(isSolid(ax, y, z + 1), isSolid(ax, y - 1, z),
                                   isSolid(ax, y - 1, z + 1));
                ao[1] = fastCalcAO(isSolid(ax, y, z + 1), isSolid(ax, y + 1, z),
                                   isSolid(ax, y + 1, z + 1));
                ao[2] = fastCalcAO(isSolid(ax, y, z - 1), isSolid(ax, y + 1, z),
                                   isSolid(ax, y + 1, z - 1));
                ao[3] = fastCalcAO(isSolid(ax, y, z - 1), isSolid(ax, y - 1, z),
                                   isSolid(ax, y - 1, z - 1));
                break;
            }
        }
    };

    auto fastAddFace = [&](BlockFace face, int x, int y, int z,
                           BlockType type) {
        uint32_t ao[VOXEL_QUAD_CORNERS];
        fastFaceAO(face, x, y, z, ao);
        // Split along the brighter diagonal
        const bool split02 = VOXEL_AO_LEVELS[ao[0]] + VOXEL_AO_LEVELS[ao[2]] >
                             VOXEL_AO_LEVELS[ao[1]] + VOXEL_AO_LEVELS[ao[3]];
        vertices.AddFace(face, (x + m_cx * SIZE) * VOXEL_POS_UNITS,
                         (y + m_cy * SIZE) * VOXEL_POS_UNITS,
                         (z + m_cz * SIZE) * VOXEL_POS_UNITS, VOXEL_POS_UNITS,
                         ao, GetBlockTextureTile(type, face),
                         split02 ? VOXEL_QUAD_START_02 : 0);
    };

    // Water mesh gets separate buffers
    VoxelVertexBuffer waterVertices;

    // Helper to check if neighbor is water
    auto isWater = [&](int x, int y, int z) -> bool {
        if (x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE)
            return IsBlockWater(static_cast<BlockType>(m_blocks[BlockIndex(x, y, z)]));
        // Check neighbors
        BlockFace face;
        int nx = x, ny = y, nz = z;
        if (y >= SIZE)     { face = BlockFace::Top;    ny = y - SIZE; }
        else if (y < 0)    { face = BlockFace::Bottom; ny = y + SIZE; }
        else if (z >= SIZE) { face = BlockFace::North; nz = z - SIZE; }
        else if (z < 0)    { face = BlockFace::South;  nz = z + SIZE; }
        else if (x >= SIZE) { face = BlockFace::East;  nx = x - SIZE; }
        else               { face = BlockFace::West;   nx = x + SIZE; }
        auto* nb = m_neighbors[static_cast<uint8_t>(face)];
        if (nb) return IsBlockWater(nb->GetBlock(nx, ny, nz));
        return false;
    };

    // Water face emitter: lowered top, unoccluded color (its g is the water
    // shader's sky light, open sky until phase 8)
    auto addWaterFace = [&](BlockFace face, int x, int y, int z) {
        waterVertices.AddFace(face, (x + m_cx * SIZE) * VOXEL_POS_UNITS,
                              (y + m_cy * SIZE) * VOXEL_POS_UNITS,
                              (z + m_cz * SIZE) * VOXEL_POS_UNITS,
                              WATER_SURFACE_UNITS, VOXEL_AO_UNOCCLUDED,
                              GetBlockTextureTile(BlockType::Water, face),
                              VOXEL_QUAD_START_02);
    };

    for (int y = 0; y < SIZE; ++y) {
        for (int z = 0; z < SIZE; ++z) {
            for (int x = 0; x < SIZE; ++x) {
                BlockType type = GetBlock(x, y, z);
                if (!IsBlockRenderable(type)) continue;

                if (IsBlockWater(type)) {
                    // Water: only emit faces adjacent to non-water, non-solid blocks (air)
                    // Top face: only if block above is not water and not opaque
                    if (!isWater(x, y+1, z) && !opaque[y+1+1][z+1][x+1])
                        addWaterFace(BlockFace::Top, x, y, z);
                    if (!isWater(x, y-1, z) && !opaque[y-1+1][z+1][x+1])
                        addWaterFace(BlockFace::Bottom, x, y, z);
                    if (!isWater(x, y, z+1) && !opaque[y+1][z+1+1][x+1])
                        addWaterFace(BlockFace::North, x, y, z);
                    if (!isWater(x, y, z-1) && !opaque[y+1][z-1+1][x+1])
                        addWaterFace(BlockFace::South, x, y, z);
                    if (!isWater(x+1, y, z) && !opaque[y+1][z+1][x+1+1])
                        addWaterFace(BlockFace::East, x, y, z);
                    if (!isWater(x-1, y, z) && !opaque[y+1][z+1][x-1+1])
                        addWaterFace(BlockFace::West, x, y, z);
                } else {
                    if (!opaque[y + 1 + 1][z + 1][x + 1]) fastAddFace(BlockFace::Top,    x, y, z, type);
                    if (!opaque[y - 1 + 1][z + 1][x + 1]) fastAddFace(BlockFace::Bottom, x, y, z, type);
                    if (!opaque[y + 1][z + 1 + 1][x + 1]) fastAddFace(BlockFace::North,  x, y, z, type);
                    if (!opaque[y + 1][z - 1 + 1][x + 1]) fastAddFace(BlockFace::South,  x, y, z, type);
                    if (!opaque[y + 1][z + 1][x + 1 + 1]) fastAddFace(BlockFace::East,   x, y, z, type);
                    if (!opaque[y + 1][z + 1][x - 1 + 1]) fastAddFace(BlockFace::West,   x, y, z, type);
                }
            }
        }
    }

    m_pendingMesh.vertices = std::move(vertices);
    m_hasPendingMesh = true;

    m_pendingWaterMesh.vertices = std::move(waterVertices);
    m_hasPendingWaterMesh = true;

    // Publish occluder runs alongside the mesh data
    for (int q = 0; q < 4; ++q) {
        for (int i = 0; i < OCC_MAX_RUNS; ++i) {
            m_occRunMinY[q][i] = occRunMinY[q][i];
            m_occRunMaxY[q][i] = occRunMaxY[q][i];
        }
    }

    m_meshBuilt = true;
}

void Chunk::AddToScene(SceneBase* scene) {
    if (m_gameObject) {
        scene->AddObject(m_gameObject);
        m_addedToScene = true;
    }
}

void Chunk::RemoveFromScene(SceneBase* scene) {
    if (m_gameObject && m_addedToScene) {
        scene->RemoveObject(m_gameObject);
        m_gameObject = nullptr;
        m_addedToScene = false;
    }
}
