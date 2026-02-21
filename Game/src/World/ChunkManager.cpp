#include "World/ChunkManager.hpp"
#include <Core/SceneBase.hpp>
#include <cmath>
#include <vector>

ChunkManager::ChunkManager() {}

ChunkManager::~ChunkManager() {
    for (auto& [coord, chunk] : m_chunks) {
        if (m_scene) chunk->RemoveFromScene(m_scene);
        delete chunk;
    }
    m_chunks.clear();
}

void ChunkManager::Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material) {
    m_scene = scene;
    m_material = material;
}

Chunk* ChunkManager::GetChunk(int cx, int cy, int cz) {
    ChunkCoord coord{cx, cy, cz};
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second : nullptr;
}

const Chunk* ChunkManager::GetChunk(int cx, int cy, int cz) const {
    ChunkCoord coord{cx, cy, cz};
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second : nullptr;
}

static int floorDiv(int a, int b) {
    return (a >= 0) ? a / b : (a - b + 1) / b;
}

static int floorMod(int a, int b) {
    return ((a % b) + b) % b;
}

BlockType ChunkManager::GetBlockAt(int worldX, int worldY, int worldZ) const {
    int cx = floorDiv(worldX, Chunk::SIZE);
    int cy = floorDiv(worldY, Chunk::SIZE);
    int cz = floorDiv(worldZ, Chunk::SIZE);

    const Chunk* chunk = GetChunk(cx, cy, cz);
    if (!chunk) return BlockType::Air;

    int lx = floorMod(worldX, Chunk::SIZE);
    int ly = floorMod(worldY, Chunk::SIZE);
    int lz = floorMod(worldZ, Chunk::SIZE);

    return chunk->GetBlock(lx, ly, lz);
}

bool ChunkManager::SetBlockAt(int worldX, int worldY, int worldZ, BlockType type) {
    int cx = floorDiv(worldX, Chunk::SIZE);
    int cy = floorDiv(worldY, Chunk::SIZE);
    int cz = floorDiv(worldZ, Chunk::SIZE);

    Chunk* chunk = GetChunk(cx, cy, cz);
    if (!chunk) return false;

    int lx = floorMod(worldX, Chunk::SIZE);
    int ly = floorMod(worldY, Chunk::SIZE);
    int lz = floorMod(worldZ, Chunk::SIZE);

    chunk->SetBlock(lx, ly, lz, type);

    // Rebuild the modified chunk
    chunk->RemoveFromScene(m_scene);
    chunk->BuildMesh(m_material);
    chunk->AddToScene(m_scene);

    // Rebuild neighbor chunks if block is on a chunk boundary
    auto rebuildNeighbor = [&](int ncx, int ncy, int ncz) {
        Chunk* neighbor = GetChunk(ncx, ncy, ncz);
        if (neighbor) {
            neighbor->RemoveFromScene(m_scene);
            neighbor->BuildMesh(m_material);
            neighbor->AddToScene(m_scene);
        }
    };

    if (lx == 0)                rebuildNeighbor(cx - 1, cy, cz);
    if (lx == Chunk::SIZE - 1)  rebuildNeighbor(cx + 1, cy, cz);
    if (ly == 0)                rebuildNeighbor(cx, cy - 1, cz);
    if (ly == Chunk::SIZE - 1)  rebuildNeighbor(cx, cy + 1, cz);
    if (lz == 0)                rebuildNeighbor(cx, cy, cz - 1);
    if (lz == Chunk::SIZE - 1)  rebuildNeighbor(cx, cy, cz + 1);

    return true;
}

VoxelRaycastResult ChunkManager::VoxelRaycast(
    const Sleak::Math::Vector3D& origin,
    const Sleak::Math::Vector3D& direction,
    float maxDist) const
{
    VoxelRaycastResult result;

    float ox = origin.GetX(), oy = origin.GetY(), oz = origin.GetZ();
    float dx = direction.GetX(), dy = direction.GetY(), dz = direction.GetZ();

    // Current voxel coordinates
    int x = static_cast<int>(std::floor(ox));
    int y = static_cast<int>(std::floor(oy));
    int z = static_cast<int>(std::floor(oz));

    // Step direction
    int stepX = (dx >= 0) ? 1 : -1;
    int stepY = (dy >= 0) ? 1 : -1;
    int stepZ = (dz >= 0) ? 1 : -1;

    // tDelta: how far along the ray to cross one full voxel in each axis
    float tDeltaX = (dx != 0.0f) ? std::abs(1.0f / dx) : 1e30f;
    float tDeltaY = (dy != 0.0f) ? std::abs(1.0f / dy) : 1e30f;
    float tDeltaZ = (dz != 0.0f) ? std::abs(1.0f / dz) : 1e30f;

    // tMax: distance to next voxel boundary in each axis
    float tMaxX = (dx != 0.0f) ? ((stepX > 0 ? (x + 1.0f - ox) : (ox - x)) * tDeltaX) : 1e30f;
    float tMaxY = (dy != 0.0f) ? ((stepY > 0 ? (y + 1.0f - oy) : (oy - y)) * tDeltaY) : 1e30f;
    float tMaxZ = (dz != 0.0f) ? ((stepZ > 0 ? (z + 1.0f - oz) : (oz - z)) * tDeltaZ) : 1e30f;

    int prevX = x, prevY = y, prevZ = z;
    float t = 0.0f;

    for (int i = 0; i < static_cast<int>(maxDist * 3.0f) + 1; ++i) {
        BlockType block = GetBlockAt(x, y, z);
        if (IsBlockSolid(block)) {
            result.hit = true;
            result.blockX = x;
            result.blockY = y;
            result.blockZ = z;
            result.placeX = prevX;
            result.placeY = prevY;
            result.placeZ = prevZ;
            result.blockType = block;
            return result;
        }

        prevX = x; prevY = y; prevZ = z;

        // Step along axis with smallest tMax
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                t = tMaxX;
                x += stepX;
                tMaxX += tDeltaX;
            } else {
                t = tMaxZ;
                z += stepZ;
                tMaxZ += tDeltaZ;
            }
        } else {
            if (tMaxY < tMaxZ) {
                t = tMaxY;
                y += stepY;
                tMaxY += tDeltaY;
            } else {
                t = tMaxZ;
                z += stepZ;
                tMaxZ += tDeltaZ;
            }
        }

        if (t > maxDist) break;
    }

    return result;
}

VoxelCollisionResult ChunkManager::ResolveVoxelCollision(
    const Sleak::Math::Vector3D& eyePos,
    float halfWidth, float height, float eyeOffset) const
{
    using namespace Sleak::Math;
    VoxelCollisionResult result;

    float feetY = eyePos.GetY() - eyeOffset;
    float posX = eyePos.GetX();
    float posZ = eyePos.GetZ();

    // Player AABB: [posX - hw, feetY, posZ - hw] to [posX + hw, feetY + height, posZ + hw]
    auto computeAABB = [&](float fx, float fy, float fz,
                           float& minX, float& minY, float& minZ,
                           float& maxX, float& maxY, float& maxZ) {
        minX = fx - halfWidth;
        maxX = fx + halfWidth;
        minY = fy;
        maxY = fy + height;
        minZ = fz - halfWidth;
        maxZ = fz + halfWidth;
    };

    // Per-block minimum penetration axis (MTV) resolution.
    // For each overlapping block, resolve on the axis with the smallest
    // penetration depth. This correctly handles both cases:
    //   - Standing on ground (tiny Y dip from gravity → resolves Y upward)
    //   - Walking into a wall (tiny X/Z entry → resolves X/Z, blocks player)
    for (int iter = 0; iter < 16; ++iter) {
        float minX, minY, minZ, maxX, maxY, maxZ;
        computeAABB(posX, feetY, posZ, minX, minY, minZ, maxX, maxY, maxZ);

        int bx0 = static_cast<int>(std::floor(minX));
        int bx1 = static_cast<int>(std::floor(maxX - 0.0001f));
        int by0 = static_cast<int>(std::floor(minY));
        int by1 = static_cast<int>(std::floor(maxY - 0.0001f));
        int bz0 = static_cast<int>(std::floor(minZ));
        int bz1 = static_cast<int>(std::floor(maxZ - 0.0001f));

        bool corrected = false;
        for (int by = by0; by <= by1; ++by) {
            for (int bz = bz0; bz <= bz1; ++bz) {
                for (int bx = bx0; bx <= bx1; ++bx) {
                    if (!IsBlockSolid(GetBlockAt(bx, by, bz))) continue;

                    float blockMinX = static_cast<float>(bx);
                    float blockMaxX = static_cast<float>(bx + 1);
                    float blockMinY = static_cast<float>(by);
                    float blockMaxY = static_cast<float>(by + 1);
                    float blockMinZ = static_cast<float>(bz);
                    float blockMaxZ = static_cast<float>(bz + 1);

                    // Recompute AABB (position may have shifted from earlier block)
                    computeAABB(posX, feetY, posZ, minX, minY, minZ, maxX, maxY, maxZ);

                    // Skip if no overlap
                    if (minX >= blockMaxX || maxX <= blockMinX ||
                        minY >= blockMaxY || maxY <= blockMinY ||
                        minZ >= blockMaxZ || maxZ <= blockMinZ)
                        continue;

                    // Penetration depth per axis (smallest push to escape)
                    float pushXPos = blockMaxX - minX;
                    float pushXNeg = maxX - blockMinX;
                    float penX = std::min(pushXPos, pushXNeg);

                    float pushYPos = blockMaxY - minY;
                    float pushYNeg = maxY - blockMinY;
                    float penY = std::min(pushYPos, pushYNeg);

                    float pushZPos = blockMaxZ - minZ;
                    float pushZNeg = maxZ - blockMinZ;
                    float penZ = std::min(pushZPos, pushZNeg);

                    // Resolve on the axis with the smallest penetration
                    if (penY <= penX && penY <= penZ) {
                        if (pushYPos < pushYNeg) {
                            feetY += pushYPos;
                            result.onGround = true;
                        } else {
                            feetY -= pushYNeg;
                            result.hitCeiling = true;
                        }
                    } else if (penX <= penZ) {
                        if (pushXPos < pushXNeg)
                            posX += pushXPos;
                        else
                            posX -= pushXNeg;
                        result.hitWall = true;
                    } else {
                        if (pushZPos < pushZNeg)
                            posZ += pushZPos;
                        else
                            posZ -= pushZNeg;
                        result.hitWall = true;
                    }
                    corrected = true;
                }
            }
        }
        if (!corrected) break;
    }

    // Compute correction as new eye position - old eye position
    float newEyeY = feetY + eyeOffset;
    result.correction = Vector3D(posX - eyePos.GetX(),
                                  newEyeY - eyePos.GetY(),
                                  posZ - eyePos.GetZ());

    return result;
}

void ChunkManager::LinkNeighbors(const ChunkCoord& coord, Chunk* chunk) {
    struct { BlockFace face; int dx, dy, dz; BlockFace opposite; } dirs[] = {
        {BlockFace::Top,    0,  1,  0, BlockFace::Bottom},
        {BlockFace::Bottom, 0, -1,  0, BlockFace::Top},
        {BlockFace::North,  0,  0,  1, BlockFace::South},
        {BlockFace::South,  0,  0, -1, BlockFace::North},
        {BlockFace::East,   1,  0,  0, BlockFace::West},
        {BlockFace::West,  -1,  0,  0, BlockFace::East},
    };

    for (auto& d : dirs) {
        Chunk* neighbor = GetChunk(coord.x + d.dx, coord.y + d.dy, coord.z + d.dz);
        if (neighbor) {
            chunk->SetNeighbor(d.face, neighbor);
            neighbor->SetNeighbor(d.opposite, chunk);
        }
    }
}

void ChunkManager::GenerateFlatTerrain(Chunk* chunk) {
    int worldBaseY = chunk->GetChunkY() * Chunk::SIZE;

    for (int y = 0; y < Chunk::SIZE; ++y) {
        int worldY = worldBaseY + y;
        if (worldY > SURFACE_HEIGHT) break;

        BlockType type;
        if (worldY < SURFACE_HEIGHT - 1)
            type = BlockType::Stone;
        else if (worldY == SURFACE_HEIGHT - 1)
            type = BlockType::Dirt;
        else
            type = BlockType::Grass;

        for (int z = 0; z < Chunk::SIZE; ++z)
            for (int x = 0; x < Chunk::SIZE; ++x)
                chunk->SetBlock(x, y, z, type);
    }
}

void ChunkManager::Update(float playerX, float playerZ) {
    int centerX = static_cast<int>(std::floor(playerX / Chunk::SIZE));
    int centerZ = static_cast<int>(std::floor(playerZ / Chunk::SIZE));

    if (centerX == m_lastCenterX && centerZ == m_lastCenterZ)
        return;

    m_lastCenterX = centerX;
    m_lastCenterZ = centerZ;

    // Remove chunks outside render distance
    std::vector<ChunkCoord> toRemove;
    for (auto& [coord, chunk] : m_chunks) {
        if (std::abs(coord.x - centerX) > m_renderDistance ||
            std::abs(coord.z - centerZ) > m_renderDistance) {
            chunk->RemoveFromScene(m_scene);
            toRemove.push_back(coord);
        }
    }
    for (auto& coord : toRemove) {
        delete m_chunks[coord];
        m_chunks.erase(coord);
    }

    // Only y=0 layer for flat world (surface fits in one chunk vertically)
    int cy = 0;

    for (int cx = centerX - m_renderDistance; cx <= centerX + m_renderDistance; ++cx) {
        for (int cz = centerZ - m_renderDistance; cz <= centerZ + m_renderDistance; ++cz) {
            ChunkCoord coord{cx, cy, cz};
            if (m_chunks.count(coord)) continue;

            auto* chunk = new Chunk(cx, cy, cz);
            m_chunks[coord] = chunk;
            GenerateFlatTerrain(chunk);
            LinkNeighbors(coord, chunk);
            chunk->BuildMesh(m_material);
            chunk->AddToScene(m_scene);
        }
    }
}
