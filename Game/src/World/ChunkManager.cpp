#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Core/SceneBase.hpp>
#include <Runtime/Material.hpp>
#include <Runtime/MeshBatch.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

ChunkManager::ChunkManager() {}

ChunkManager::~ChunkManager() {
    StopWorkers();
    // MeshHandle GPU buffers are released automatically via RefPtr
    m_columns.clear();
    for (Chunk* chunk : m_activeChunks) {
        if (chunk) delete chunk;
    }
    m_activeChunks.clear();
    m_chunkGrid.clear();
}

void ChunkManager::SetMultithreaded(bool enabled) {
    if (enabled == m_multithreaded) return;
    m_multithreaded = enabled;
    if (enabled) {
        StartWorkers();
    } else {
        StopWorkers();
    }
}

void ChunkManager::StartWorkers() {
    if (!m_workers.empty()) return;
    m_shutdown.store(false);
    int count = static_cast<int>(std::thread::hardware_concurrency()) - 2;
    if (count < 2) count = 2;
    if (count > 12) count = 12;
    for (int i = 0; i < count; ++i)
        m_workers.emplace_back(&ChunkManager::WorkerThread, this);
}

void ChunkManager::StopWorkers() {
    if (m_workers.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_shutdown.store(true);
    }
    m_taskCV.notify_all();
    for (auto& w : m_workers)
        w.join();
    m_workers.clear();

    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_taskQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_readyMutex);
        m_readyQueue.clear();
    }
    m_chunksNeedingRemesh.clear();
}

void ChunkManager::WorkerThread() {
    std::vector<Chunk*> localBatch;
    localBatch.reserve(8);
    while (true) {
        localBatch.clear();
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_taskCV.wait(lock, [this] { return m_shutdown.load() || !m_taskQueue.empty(); });
            if (m_shutdown.load() && m_taskQueue.empty()) return;

            // Steal up to 8 chunks at once
            for (int i = 0; i < 8 && !m_taskQueue.empty(); ++i) {
                localBatch.push_back(m_taskQueue.back());
                m_taskQueue.pop_back();
            }
        }

        for (Chunk* chunk : localBatch) {
            if (chunk->NeedsGeneration()) {
                m_generator.Generate(chunk);
                chunk->SetNeedsGeneration(false);
            }
            chunk->GenerateMeshData();
        }

        {
            std::lock_guard<std::mutex> lock(m_readyMutex);
            for (Chunk* chunk : localBatch) {
                m_readyQueue.push_back(chunk);
            }
        }
    }
}

void ChunkManager::Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material) {
    m_scene = scene;
    m_material = material;
    m_drawDistance = static_cast<float>(m_renderDistance * Chunk::SIZE);
    m_drawDistSq = m_drawDistance * m_drawDistance;
    BuildLoadSpiral();
}

int ChunkManager::GetGridIndex(int cx, int cy, int cz) const {
    if (cy < WorldGenerator::MIN_CHUNK_Y || cy > WorldGenerator::MAX_CHUNK_Y) return -1;
    int px = (cx % m_gridWidth); if (px < 0) px += m_gridWidth;
    int py = cy - WorldGenerator::MIN_CHUNK_Y;
    int pz = (cz % m_gridWidth); if (pz < 0) pz += m_gridWidth;
    return px + pz * m_gridWidth + py * m_gridWidth * m_gridWidth;
}

void ChunkManager::BuildLoadSpiral() {
    m_gridWidth = (m_renderDistance + 2) * 2;
    int totalSize = m_gridWidth * m_gridWidth * m_gridHeight;
    m_chunkGrid.assign(totalSize, nullptr);

    for (Chunk* chunk : m_activeChunks) {
        if (!chunk) continue;
        int idx = GetGridIndex(chunk->GetChunkX(), chunk->GetChunkY(), chunk->GetChunkZ());
        if (idx >= 0) m_chunkGrid[idx] = chunk;
    }

    m_loadSpiral.clear();
    for (int x = -m_renderDistance; x <= m_renderDistance; ++x) {
        for (int z = -m_renderDistance; z <= m_renderDistance; ++z) {
            // Include corners for squared render distance, or make it circular:
            // if (x*x + z*z <= m_renderDistance * m_renderDistance)
            m_loadSpiral.push_back({x, z});
        }
    }

    std::sort(m_loadSpiral.begin(), m_loadSpiral.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        return (a.first * a.first + a.second * a.second) < (b.first * b.first + b.second * b.second);
    });
}

int ChunkManager::GetCachedColumnMaxCy(int cx, int cz) {
    uint64_t key = PackColumnXZ(cx, cz);
    auto it = m_columnMaxCyCache.find(key);
    if (it != m_columnMaxCyCache.end()) return it->second;
    int maxCy = m_generator.GetMaxFilledChunkY(cx, cz);
    m_columnMaxCyCache.emplace(key, maxCy);
    return maxCy;
}

Chunk* ChunkManager::GetChunk(int cx, int cy, int cz) {
    int idx = GetGridIndex(cx, cy, cz);
    if (idx < 0) return nullptr;
    Chunk* ch = m_chunkGrid[idx];
    if (ch && ch->GetChunkX() == cx && ch->GetChunkY() == cy && ch->GetChunkZ() == cz) return ch;
    return nullptr;
}

const Chunk* ChunkManager::GetChunk(int cx, int cy, int cz) const {
    int idx = GetGridIndex(cx, cy, cz);
    if (idx < 0) return nullptr;
    Chunk* ch = m_chunkGrid[idx];
    if (ch && ch->GetChunkX() == cx && ch->GetChunkY() == cy && ch->GetChunkZ() == cz) return ch;
    return nullptr;
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
    if (!chunk) {
        // Create the chunk on-demand if within valid Y range
        // (it was likely skipped by IsChunkAboveTerrain)
        if (cy < WorldGenerator::MIN_CHUNK_Y || cy > WorldGenerator::MAX_CHUNK_Y)
            return false;
        chunk = new Chunk(cx, cy, cz);
        int idx = GetGridIndex(cx, cy, cz);
        if (idx >= 0) {
            m_chunkGrid[idx] = chunk;
            chunk->SetActiveIndex(static_cast<int>(m_activeChunks.size()));
            m_activeChunks.push_back(chunk);
        }
        chunk->SetNeedsGeneration(false);
        LinkNeighbors({cx, cy, cz}, chunk);
    }

    int lx = floorMod(worldX, Chunk::SIZE);
    int ly = floorMod(worldY, Chunk::SIZE);
    int lz = floorMod(worldZ, Chunk::SIZE);

    chunk->SetBlock(lx, ly, lz, type);
    chunk->SetDirty(true);

    std::unordered_set<ColumnKey, ColumnKeyHash> affectedColumns;

    // Only rebuild mesh if the chunk is not being processed by a worker thread
    if (chunk->IsInFlight()) {
        chunk->SetNeedsMeshRebuild(true);
        m_chunksNeedingRemesh.insert({cx, cy, cz});
    } else {
        chunk->GenerateMeshData();
        affectedColumns.insert({cx, ChunkYToBand(cy), cz});
    }

    auto rebuildNeighbor = [&](int ncx, int ncy, int ncz) {
        Chunk* neighbor = GetChunk(ncx, ncy, ncz);
        if (neighbor) {
            if (neighbor->IsInFlight()) {
                neighbor->SetNeedsMeshRebuild(true);
                m_chunksNeedingRemesh.insert({ncx, ncy, ncz});
            } else {
                neighbor->GenerateMeshData();
                affectedColumns.insert({ncx, ChunkYToBand(ncy), ncz});
            }
        }
    };

    if (lx == 0)                rebuildNeighbor(cx - 1, cy, cz);
    if (lx == Chunk::SIZE - 1)  rebuildNeighbor(cx + 1, cy, cz);
    if (ly == 0)                rebuildNeighbor(cx, cy - 1, cz);
    if (ly == Chunk::SIZE - 1)  rebuildNeighbor(cx, cy + 1, cz);
    if (lz == 0)                rebuildNeighbor(cx, cy, cz - 1);
    if (lz == Chunk::SIZE - 1)  rebuildNeighbor(cx, cy, cz + 1);

    for (auto& col : affectedColumns)
        RebuildColumnMesh(col.x, col.yBand, col.z, false);  // sync — user interaction, must be immediate

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

    int x = static_cast<int>(std::floor(ox));
    int y = static_cast<int>(std::floor(oy));
    int z = static_cast<int>(std::floor(oz));

    int stepX = (dx >= 0) ? 1 : -1;
    int stepY = (dy >= 0) ? 1 : -1;
    int stepZ = (dz >= 0) ? 1 : -1;

    float tDeltaX = (dx != 0.0f) ? std::abs(1.0f / dx) : 1e30f;
    float tDeltaY = (dy != 0.0f) ? std::abs(1.0f / dy) : 1e30f;
    float tDeltaZ = (dz != 0.0f) ? std::abs(1.0f / dz) : 1e30f;

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

                    computeAABB(posX, feetY, posZ, minX, minY, minZ, maxX, maxY, maxZ);

                    if (minX >= blockMaxX || maxX <= blockMinX ||
                        minY >= blockMaxY || maxY <= blockMinY ||
                        minZ >= blockMaxZ || maxZ <= blockMinZ)
                        continue;

                    float pushXPos = blockMaxX - minX;
                    float pushXNeg = maxX - blockMinX;
                    float penX = std::min(pushXPos, pushXNeg);

                    float pushYPos = blockMaxY - minY;
                    float pushYNeg = maxY - blockMinY;
                    float penY = std::min(pushYPos, pushYNeg);

                    float pushZPos = blockMaxZ - minZ;
                    float pushZNeg = maxZ - blockMinZ;
                    float penZ = std::min(pushZPos, pushZNeg);

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
            if (!neighbor->IsInFlight()) {
                neighbor->SetNeighbor(d.opposite, chunk);
                if (neighbor->IsMeshBuilt() && !neighbor->NeedsMeshRebuild()
                    && !m_generator.IsChunkFullySolid(neighbor)) {
                    neighbor->SetNeedsMeshRebuild(true);
                    m_chunksNeedingRemesh.insert({coord.x + d.dx, coord.y + d.dy, coord.z + d.dz});
                }
            }
        }
    }
}

void ChunkManager::UnlinkNeighbors(const ChunkCoord& coord, Chunk* chunk) {
    struct { BlockFace face; int dx, dy, dz; BlockFace opposite; } dirs[] = {
        {BlockFace::Top,    0,  1,  0, BlockFace::Bottom},
        {BlockFace::Bottom, 0, -1,  0, BlockFace::Top},
        {BlockFace::North,  0,  0,  1, BlockFace::South},
        {BlockFace::South,  0,  0, -1, BlockFace::North},
        {BlockFace::East,   1,  0,  0, BlockFace::West},
        {BlockFace::West,  -1,  0,  0, BlockFace::East},
    };

    for (auto& d : dirs) {
        chunk->SetNeighbor(d.face, nullptr);
        Chunk* neighbor = GetChunk(coord.x + d.dx, coord.y + d.dy, coord.z + d.dz);
        if (neighbor && !neighbor->IsInFlight())
            neighbor->SetNeighbor(d.opposite, nullptr);
    }
}

bool ChunkManager::IsNeighborOfInFlight(const ChunkCoord& coord) const {
    static const int offsets[][3] = {{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};
    for (auto& o : offsets) {
        const Chunk* neighbor = GetChunk(coord.x + o[0], coord.y + o[1], coord.z + o[2]);
        if (neighbor && neighbor->IsInFlight()) return true;
    }
    return false;
}

void ChunkManager::RebuildColumnMesh(int cx, int yBand, int cz, bool allowDefer) {
    ColumnKey key{cx, yBand, cz};

    int bandMinY = yBand * BAND_SIZE;
    int bandMaxY = bandMinY + BAND_SIZE - 1;

    Sleak::VertexGroup mergedVerts;
    Sleak::IndexGroup mergedIndices;
    Sleak::VertexGroup mergedWaterVerts;
    Sleak::IndexGroup mergedWaterIndices;

    for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
        Chunk* chunk = GetChunk(cx, cy, cz);
        // Skip chunks not ready (in-flight or not yet generated)
        if (!chunk || chunk->IsInFlight() || chunk->NeedsGeneration()) continue;

        // Mesh data was consumed — regenerate it.
        if (!chunk->HasPendingMesh()) {
            if (m_multithreaded && allowDefer) {
                chunk->SetInFlight(true);
                {
                    std::lock_guard<std::mutex> lock(m_taskMutex);
                    m_taskQueue.push_back(chunk);
                }
                m_taskCV.notify_one();
                m_dirtyColumns.insert(key);
                return;
            }
            chunk->GenerateMeshData();
        }

        // Merge opaque mesh
        {
            auto& md = chunk->GetPendingMeshData();
            chunk->ClearPendingMesh();

            if (md.vertices.GetSize() > 0) {
                uint32_t baseVertex = static_cast<uint32_t>(mergedVerts.GetSize());
                const Sleak::Vertex* vdata = md.vertices.GetData();
                for (size_t i = 0; i < md.vertices.GetSize(); ++i)
                    mergedVerts.AddVertex(vdata[i]);
                const uint32_t* idata = md.indices.GetData();
                for (size_t i = 0; i < md.indices.GetSize(); ++i)
                    mergedIndices.add(idata[i] + baseVertex);
            }
            md.vertices.release();
            md.indices.release();
        }

        // Merge water mesh
        {
            auto& wd = chunk->GetPendingWaterMeshData();
            chunk->ClearPendingWaterMesh();

            if (wd.vertices.GetSize() > 0) {
                uint32_t baseVertex = static_cast<uint32_t>(mergedWaterVerts.GetSize());
                const Sleak::Vertex* vdata = wd.vertices.GetData();
                for (size_t i = 0; i < wd.vertices.GetSize(); ++i)
                    mergedWaterVerts.AddVertex(vdata[i]);
                const uint32_t* idata = wd.indices.GetData();
                for (size_t i = 0; i < wd.indices.GetSize(); ++i)
                    mergedWaterIndices.add(idata[i] + baseVertex);
            }
            wd.vertices.release();
            wd.indices.release();
        }
    }

    m_columns.erase(key);

    if (mergedVerts.GetSize() == 0 && mergedWaterVerts.GetSize() == 0) return;

    ColumnMesh col;
    if (mergedVerts.GetSize() > 0)
        col.mesh = Sleak::MeshBatch::CreateMesh(mergedVerts, mergedIndices);
    if (mergedWaterVerts.GetSize() > 0)
        col.waterMesh = Sleak::MeshBatch::CreateMesh(mergedWaterVerts, mergedWaterIndices);
    col.visible = true;
    m_columns[key] = std::move(col);
}

void ChunkManager::ForceUnloadChunk(Chunk* chunk) {
    if (!chunk) return;
    int idx = GetGridIndex(chunk->GetChunkX(), chunk->GetChunkY(), chunk->GetChunkZ());
    if (idx >= 0 && m_chunkGrid[idx] == chunk) {
        m_chunkGrid[idx] = nullptr;
    }
    
    int activeIdx = chunk->GetActiveIndex();
    if (activeIdx >= 0 && activeIdx < static_cast<int>(m_activeChunks.size())) {
        Chunk* last = m_activeChunks.back();
        m_activeChunks[activeIdx] = last;
        if (last) last->SetActiveIndex(activeIdx);
        m_activeChunks.pop_back();
    }
    chunk->SetActiveIndex(-1);
}

void ChunkManager::Update(float playerX, float playerY, float playerZ) {
    int centerX = static_cast<int>(std::floor(playerX / Chunk::SIZE));
    int centerY = static_cast<int>(std::floor(playerY / Chunk::SIZE));
    int centerZ = static_cast<int>(std::floor(playerZ / Chunk::SIZE));
    m_lastCenterY = centerY;

    if (centerX != m_lastCenterX || centerZ != m_lastCenterZ) {
        // Save previous center before updating, so we can compute exiting slabs.
        int prevCX = (m_lastCenterX == INT_MAX) ? centerX : m_lastCenterX;
        int prevCZ = (m_lastCenterZ == INT_MAX) ? centerZ : m_lastCenterZ;
        m_lastCenterX = centerX;
        m_lastCenterZ = centerZ;

        // Queue out-of-range chunks for gradual unloading.
        // When the player moves by a small delta we only need to check the slabs
        // that just left the render range — O(rd * height) instead of O(N).
        // Fall back to a full scan on large teleports.
        int dX = std::abs(centerX - prevCX);
        int dZ = std::abs(centerZ - prevCZ);
        bool largeTeleport = (dX > m_renderDistance * 2 || dZ > m_renderDistance * 2);
        if (largeTeleport) {
            for (Chunk* chunk : m_activeChunks) {
                if (!chunk) continue;
                int cx = chunk->GetChunkX(), cy = chunk->GetChunkY(), cz = chunk->GetChunkZ();
                if (std::abs(cx - centerX) > m_renderDistance ||
                    std::abs(cz - centerZ) > m_renderDistance)
                    m_pendingUnload.push_back({cx, cy, cz});
            }
        } else {
            // Unload X-axis slabs that just left view
            auto unloadSlabX = [&](int cx) {
                for (int cz2 = prevCZ - m_renderDistance; cz2 <= prevCZ + m_renderDistance; ++cz2)
                    for (int cy = WorldGenerator::MIN_CHUNK_Y; cy <= WorldGenerator::MAX_CHUNK_Y; ++cy)
                        if (GetChunk(cx, cy, cz2)) m_pendingUnload.push_back({cx, cy, cz2});
            };
            if (centerX > prevCX)
                for (int x = prevCX - m_renderDistance; x < centerX - m_renderDistance; ++x) unloadSlabX(x);
            else if (centerX < prevCX)
                for (int x = centerX + m_renderDistance + 1; x <= prevCX + m_renderDistance; ++x) unloadSlabX(x);

            // Unload Z-axis slabs that just left view
            auto unloadSlabZ = [&](int cz2) {
                for (int cx = centerX - m_renderDistance; cx <= centerX + m_renderDistance; ++cx)
                    for (int cy = WorldGenerator::MIN_CHUNK_Y; cy <= WorldGenerator::MAX_CHUNK_Y; ++cy)
                        if (GetChunk(cx, cy, cz2)) m_pendingUnload.push_back({cx, cy, cz2});
            };
            if (centerZ > prevCZ)
                for (int z = prevCZ - m_renderDistance; z < centerZ - m_renderDistance; ++z) unloadSlabZ(z);
            else if (centerZ < prevCZ)
                for (int z = centerZ + m_renderDistance + 1; z <= prevCZ + m_renderDistance; ++z) unloadSlabZ(z);
        }

        m_pendingLoad.clear();
        m_pendingLoad.reserve(m_loadSpiral.size() * (WorldGenerator::MAX_CHUNK_Y - WorldGenerator::MIN_CHUNK_Y + 1));

        // Spiral generates coordinates from closest to furthest.
        // We push them in reverse so the pop_back() takes the closest chunks first!
        // GetCachedColumnMaxCy() computes GetMaxFilledChunkY once per XZ column and caches
        // the result permanently (terrain is deterministic), replacing O(8) IsChunkAboveTerrain
        // calls (each with 9×6 FBM evaluations) with a single cached lookup per column.
        for (auto it = m_loadSpiral.rbegin(); it != m_loadSpiral.rend(); ++it) {
            int cx = centerX + it->first;
            int cz = centerZ + it->second;
            int maxCy = GetCachedColumnMaxCy(cx, cz);
            for (int cy = WorldGenerator::MIN_CHUNK_Y; cy <= maxCy; ++cy) {
                if (!GetChunk(cx, cy, cz))
                    m_pendingLoad.push_back({cx, cy, cz});
            }
        }
    }

    // Process pending unloads gradually (rate-limited)
    {
        int unloaded = 0;
        std::unordered_set<ColumnKey, ColumnKeyHash> columnsToCheck;
        while (unloaded < m_chunksPerFrame && !m_pendingUnload.empty()) {
            ChunkCoord coord = m_pendingUnload.back();
            m_pendingUnload.pop_back();

            Chunk* chunk = GetChunk(coord.x, coord.y, coord.z);
            if (!chunk) continue;

            if (chunk->IsInFlight() || IsNeighborOfInFlight(coord))
                continue;

            columnsToCheck.insert({coord.x, ChunkYToBand(coord.y), coord.z});
            UnlinkNeighbors(coord, chunk);
            ForceUnloadChunk(chunk);
            delete chunk;
            ++unloaded;
        }

        // Remove column meshes for bands that lost all their chunks
        for (auto& colKey : columnsToCheck) {
            bool hasChunks = false;
            int bandMinY = colKey.yBand * BAND_SIZE;
            int bandMaxY = bandMinY + BAND_SIZE - 1;
            for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
                if (GetChunk(colKey.x, cy, colKey.z)) { hasChunks = true; break; }
            }
            if (!hasChunks) {
                m_columns.erase(colKey);
                m_dirtyColumns.erase(colKey);  // Don't rebuild a column with no chunks
            }
        }
    }

    if (m_multithreaded) {
        // Phase 1: Process completed chunks from workers — mark bands dirty
        // and re-link neighbors that may have been loaded while in-flight.
        // We carefully check whether pointers actually changed before marking
        // anything for remesh, to avoid cascading unnecessary rebuilds.
        {
            std::vector<Chunk*> ready;
            {
                std::lock_guard<std::mutex> lock(m_readyMutex);
                ready.swap(m_readyQueue);
            }
            static const struct { BlockFace face; int dx, dy, dz; BlockFace opposite; } dirs[] = {
                {BlockFace::Top,    0,  1,  0, BlockFace::Bottom},
                {BlockFace::Bottom, 0, -1,  0, BlockFace::Top},
                {BlockFace::North,  0,  0,  1, BlockFace::South},
                {BlockFace::South,  0,  0, -1, BlockFace::North},
                {BlockFace::East,   1,  0,  0, BlockFace::West},
                {BlockFace::West,  -1,  0,  0, BlockFace::East},
            };

            for (auto* chunk : ready) {
                chunk->SetInFlight(false);

                ChunkCoord coord{chunk->GetChunkX(), chunk->GetChunkY(), chunk->GetChunkZ()};
                int neighborsBefore = chunk->CountNeighbors();

                // Re-link: set our neighbor pointers, and for each neighbor
                // that doesn't already point back to us, set the back-pointer
                // and mark it for remesh (only when the pointer actually changed).
                for (auto& d : dirs) {
                    Chunk* neighbor = GetChunk(coord.x + d.dx, coord.y + d.dy, coord.z + d.dz);
                    if (!neighbor) continue;
                    chunk->SetNeighbor(d.face, neighbor);
                    if (!neighbor->IsInFlight()) {
                        int nBefore = neighbor->CountNeighbors();
                        neighbor->SetNeighbor(d.opposite, chunk);
                        int nAfter = neighbor->CountNeighbors();
                        // Only remesh if the neighbor gained a genuinely new pointer
                        if (nAfter > nBefore && neighbor->IsMeshBuilt()
                            && !neighbor->NeedsMeshRebuild()
                            && !m_generator.IsChunkFullySolid(neighbor)) {
                            neighbor->SetNeedsMeshRebuild(true);
                            m_chunksNeedingRemesh.insert({coord.x + d.dx, coord.y + d.dy, coord.z + d.dz});
                        }
                    }
                }

                int neighborsAfter = chunk->CountNeighbors();
                if (neighborsAfter > neighborsBefore) {
                    chunk->SetNeedsMeshRebuild(true);
                    m_chunksNeedingRemesh.insert(coord);
                }

                if (chunk->HasPendingMesh()) {
                    m_dirtyColumns.insert({chunk->GetChunkX(),
                                           ChunkYToBand(chunk->GetChunkY()),
                                           chunk->GetChunkZ()});
                }
            }
        }

        // Phase 2: Dispatch new chunks to workers
        int dispatchBudget = m_chunksPerFrame;

        std::vector<Chunk*> batch;
        int dispatched = 0;
        while (dispatched < dispatchBudget && !m_pendingLoad.empty()) {
            ChunkCoord coord = m_pendingLoad.back();
            m_pendingLoad.pop_back();

            if (GetChunk(coord.x, coord.y, coord.z)) continue;

            auto* chunk = new Chunk(coord.x, coord.y, coord.z);
            int idx = GetGridIndex(coord.x, coord.y, coord.z);
            if (idx >= 0) {
                if (m_chunkGrid[idx] != nullptr) {
                    Chunk* stale = m_chunkGrid[idx];
                    UnlinkNeighbors({stale->GetChunkX(), stale->GetChunkY(), stale->GetChunkZ()}, stale);
                    ForceUnloadChunk(stale);
                    delete stale;
                }
                m_chunkGrid[idx] = chunk;
                chunk->SetActiveIndex(static_cast<int>(m_activeChunks.size()));
                m_activeChunks.push_back(chunk);
            }
            int64_t key = PackCoord(coord.x, coord.y, coord.z);
            auto savedIt = m_savedBlockData.find(key);
            if (savedIt != m_savedBlockData.end()) {
                std::memcpy(const_cast<uint8_t*>(chunk->GetBlockData()),
                            savedIt->second.data(), 4096);
                chunk->SetNeedsGeneration(false);
            }
            LinkNeighbors(coord, chunk);
            batch.push_back(chunk);
            ++dispatched;
        }

        if (!batch.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_taskMutex);
                for (auto* chunk : batch) {
                    chunk->SetInFlight(true);
                    m_taskQueue.push_back(chunk);
                }
            }
            m_taskCV.notify_all();
        }

        // Phase 3: Dispatch remesh requests to workers
        // Uses m_chunksNeedingRemesh (O(k)) instead of scanning all chunks (O(n))
        {
            std::vector<Chunk*> remeshBatch;
            int remeshBudget = m_chunksPerFrame;
            auto it = m_chunksNeedingRemesh.begin();
            while (it != m_chunksNeedingRemesh.end() && remeshBudget > 0) {
                Chunk* ch = GetChunk(it->x, it->y, it->z);
                if (!ch) {
                    it = m_chunksNeedingRemesh.erase(it);
                    continue;
                }
                if (!ch->NeedsMeshRebuild()) {
                    it = m_chunksNeedingRemesh.erase(it);
                    continue;
                }
                if (ch->IsInFlight() || ch->NeedsGeneration()) {
                    ++it;  // still busy, retry next frame
                    continue;
                }
                ch->SetNeedsMeshRebuild(false);
                remeshBatch.push_back(ch);
                it = m_chunksNeedingRemesh.erase(it);
                --remeshBudget;
            }
            if (!remeshBatch.empty()) {
                std::lock_guard<std::mutex> lock(m_taskMutex);
                for (auto* ch : remeshBatch) {
                    ch->SetInFlight(true);
                    m_taskQueue.push_back(ch);
                }
                m_taskCV.notify_all();
            }
        }

        // Phase 4: Rebuild dirty band column meshes (GPU upload)
        // Skip columns where any chunk is still in-flight to prevent
        // flickering (the old column mesh stays visible until remesh is done).
        // Adaptive budget: process more when backlog is large.
        {
            // Adaptive budget: process more columns when backlog is large
            // (caps at 64 to avoid single-frame GPU upload spikes)
            int uploadBudget = std::min(64, std::max(m_uploadsPerFrame,
                                                     (int)m_dirtyColumns.size() / 4));

            std::vector<ColumnKey> toRebuild;
            {
                int count = 0;
                for (auto it = m_dirtyColumns.begin();
                     it != m_dirtyColumns.end() && count < uploadBudget; ) {
                    // Defer if any chunk in this band is still in-flight
                    bool anyInFlight = false;
                    int bandMinY = it->yBand * BAND_SIZE;
                    int bandMaxY = bandMinY + BAND_SIZE - 1;
                    for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
                        Chunk* ch = GetChunk(it->x, cy, it->z);
                        if (ch && ch->IsInFlight()) { anyInFlight = true; break; }
                    }
                    if (anyInFlight) {
                        ++it;  // keep dirty, try again next frame
                        continue;
                    }
                    toRebuild.push_back(*it);
                    it = m_dirtyColumns.erase(it);
                    ++count;
                }
            }

            for (auto& key : toRebuild) {
                RebuildColumnMesh(key.x, key.yBand, key.z);
            }
        }
    } else {
        // Synchronous path
        int built = 0;
        std::unordered_set<ColumnKey, ColumnKeyHash> syncDirtyColumns;
        while (built < m_chunksPerFrame && !m_pendingLoad.empty()) {
            ChunkCoord coord = m_pendingLoad.back();
            m_pendingLoad.pop_back();

            if (GetChunk(coord.x, coord.y, coord.z)) continue;

            auto* chunk = new Chunk(coord.x, coord.y, coord.z);
            int idx = GetGridIndex(coord.x, coord.y, coord.z);
            if (idx >= 0) {
                if (m_chunkGrid[idx] != nullptr) {
                    Chunk* stale = m_chunkGrid[idx];
                    UnlinkNeighbors({stale->GetChunkX(), stale->GetChunkY(), stale->GetChunkZ()}, stale);
                    ForceUnloadChunk(stale);
                    delete stale;
                }
                m_chunkGrid[idx] = chunk;
                chunk->SetActiveIndex(static_cast<int>(m_activeChunks.size()));
                m_activeChunks.push_back(chunk);
            }
            int64_t key = PackCoord(coord.x, coord.y, coord.z);
            auto savedIt = m_savedBlockData.find(key);
            if (savedIt != m_savedBlockData.end()) {
                std::memcpy(const_cast<uint8_t*>(chunk->GetBlockData()),
                            savedIt->second.data(), 4096);
            } else {
                m_generator.Generate(chunk);
            }
            chunk->SetNeedsGeneration(false);
            LinkNeighbors(coord, chunk);
            chunk->GenerateMeshData();
            syncDirtyColumns.insert({coord.x, ChunkYToBand(coord.y), coord.z});
            ++built;
        }

        int rebuilt = 0;
        {
            auto it = m_chunksNeedingRemesh.begin();
            while (it != m_chunksNeedingRemesh.end() && rebuilt < m_chunksPerFrame) {
                Chunk* ch = GetChunk(it->x, it->y, it->z);
                if (!ch || !ch->NeedsMeshRebuild() || ch->NeedsGeneration()) {
                    it = m_chunksNeedingRemesh.erase(it);
                    continue;
                }
                ch->SetNeedsMeshRebuild(false);
                ch->GenerateMeshData();
                syncDirtyColumns.insert({ch->GetChunkX(), ChunkYToBand(ch->GetChunkY()), ch->GetChunkZ()});
                it = m_chunksNeedingRemesh.erase(it);
                ++rebuilt;
            }
        }

        for (auto& col : syncDirtyColumns)
            RebuildColumnMesh(col.x, col.yBand, col.z);
    }

    FrustumCull();
}

void ChunkManager::FlushPendingChunks() {
    // Pass 1: Generate all chunks and link neighbors
    std::vector<ChunkCoord> generated;
    while (!m_pendingLoad.empty()) {
        ChunkCoord coord = m_pendingLoad.back();
        m_pendingLoad.pop_back();
        m_pendingSet.erase(coord);

        if (GetChunk(coord.x, coord.y, coord.z)) continue;

        auto* chunk = new Chunk(coord.x, coord.y, coord.z);
        int idx = GetGridIndex(coord.x, coord.y, coord.z);
        if (idx >= 0) {
            if (m_chunkGrid[idx] != nullptr) {
                Chunk* stale = m_chunkGrid[idx];
                UnlinkNeighbors({stale->GetChunkX(), stale->GetChunkY(), stale->GetChunkZ()}, stale);
                ForceUnloadChunk(stale);
                delete stale;
            }
            m_chunkGrid[idx] = chunk;
            chunk->SetActiveIndex(static_cast<int>(m_activeChunks.size()));
            m_activeChunks.push_back(chunk);
        }
        int64_t key = PackCoord(coord.x, coord.y, coord.z);
        auto savedIt = m_savedBlockData.find(key);
        if (savedIt != m_savedBlockData.end()) {
            std::memcpy(const_cast<uint8_t*>(chunk->GetBlockData()),
                        savedIt->second.data(), 4096);
        } else {
            m_generator.Generate(chunk);
        }
        chunk->SetNeedsGeneration(false);
        LinkNeighbors(coord, chunk);
        generated.push_back(coord);
    }

    // Pass 2: Mesh all chunks (now that all neighbors exist and are linked)
    std::unordered_set<ColumnKey, ColumnKeyHash> flushDirtyColumns;
    for (auto& coord : generated) {
        Chunk* chunk = GetChunk(coord.x, coord.y, coord.z);
        if (chunk) {
            chunk->GenerateMeshData();
            flushDirtyColumns.insert({coord.x, ChunkYToBand(coord.y), coord.z});
        }
    }

    // Pass 3: Build column meshes
    for (auto& col : flushDirtyColumns)
        RebuildColumnMesh(col.x, col.yBand, col.z);
}

int64_t ChunkManager::PackCoord(int32_t cx, int32_t cy, int32_t cz) {
    uint64_t ux = static_cast<uint32_t>(cx);
    uint64_t uy = static_cast<uint32_t>(cy) & 0xFFFF;
    uint64_t uz = static_cast<uint32_t>(cz);
    return static_cast<int64_t>((ux << 32) | (uy << 16) | (uz & 0xFFFF));
}

std::vector<ChunkManager::DirtyChunkInfo> ChunkManager::GetDirtyChunks() const {
    std::vector<DirtyChunkInfo> result;
    for (Chunk* chunk : m_activeChunks) {
        if (chunk && chunk->IsDirty()) {
            result.push_back({chunk->GetChunkX(), chunk->GetChunkY(), chunk->GetChunkZ(), chunk->GetBlockData()});
        }
    }
    return result;
}

void ChunkManager::ClearDirtyFlags() {
    for (Chunk* chunk : m_activeChunks)
        if (chunk) chunk->SetDirty(false);
}

void ChunkManager::LoadChunkData(const std::unordered_map<int64_t, std::array<uint8_t, 4096>>& data) {
    m_savedBlockData = data;
}

void ChunkManager::ForceReload() {
    bool wasMultithreaded = m_multithreaded;
    if (wasMultithreaded) StopWorkers();

    m_columns.clear();
    m_dirtyColumns.clear();
    m_chunksNeedingRemesh.clear();

    // Remove all chunks
    for (Chunk* chunk : m_activeChunks) {
        if (chunk) delete chunk;
    }
    m_activeChunks.clear();
    m_chunkGrid.assign(m_chunkGrid.size(), nullptr);
    m_pendingLoad.clear();
    m_pendingSet.clear();
    m_lastCenterX = INT_MAX;
    m_lastCenterY = INT_MAX;
    m_lastCenterZ = INT_MAX;

    if (wasMultithreaded) StartWorkers();
}

void ChunkManager::FrustumCull() {
    const auto& frustum = Sleak::Camera::GetMainViewFrustum();
    const auto& camPos = Sleak::Camera::GetMainCameraPosition();
    float camX = camPos.GetX();
    float camY = camPos.GetY();
    float camZ = camPos.GetZ();

    for (auto& [key, col] : m_columns) {
        if (!col.mesh.IsValid() && !col.waterMesh.IsValid()) { col.visible = false; continue; }

        float minX = static_cast<float>(key.x * Chunk::SIZE);
        float minY = static_cast<float>(key.yBand * BAND_SIZE * Chunk::SIZE);
        float minZ = static_cast<float>(key.z * Chunk::SIZE);
        float maxX = minX + Chunk::SIZE;
        float maxY = minY + BAND_SIZE * Chunk::SIZE;
        float maxZ = minZ + Chunk::SIZE;

        // Horizontal-only distance check (XZ cylinder) so columns stay visible
        // when the player is high above the terrain
        float dx = (camX < minX) ? (minX - camX) : (camX > maxX) ? (camX - maxX) : 0.0f;
        float dz = (camZ < minZ) ? (minZ - camZ) : (camZ > maxZ) ? (camZ - maxZ) : 0.0f;
        float distSq = dx * dx + dz * dz;

        if (distSq > m_drawDistSq) {
            col.visible = false;
            continue;
        }

        // Force-render columns near the player regardless of camera frustum.
        // This ensures terrain above caves/enclosed spaces is always in the
        // shadow map, preventing sunlight from leaking through terrain.
        constexpr float SHADOW_FORCE_DIST = 48.0f;
        if (distSq <= SHADOW_FORCE_DIST * SHADOW_FORCE_DIST) {
            col.visible = true;
            continue;
        }

        col.visible = frustum.IsAABBVisible(
            Sleak::Math::Vector3D(minX, minY, minZ),
            Sleak::Math::Vector3D(maxX, maxY, maxZ));
    }
}

void ChunkManager::RenderColumns() {
    Sleak::MeshBatch::BeginBatch(m_material.get());
    for (auto& [key, col] : m_columns) {
        if (col.visible && col.mesh.IsValid())
            Sleak::MeshBatch::Draw(col.mesh);
    }
    Sleak::MeshBatch::EndBatch();
}

void ChunkManager::RenderWater() {
    if (!m_waterMaterial) return;
    Sleak::MeshBatch::BeginBatch(m_waterMaterial.get());
    for (auto& [key, col] : m_columns) {
        if (col.visible && col.waterMesh.IsValid())
            Sleak::MeshBatch::Draw(col.waterMesh);
    }
    Sleak::MeshBatch::EndBatch();
}

// ── Heightmap cache ──────────────────────────────────────────────────────────
// Format: magic(4) seed(4) count(4) [cx(4) cz(4) maxCy(4)] * count
// Tied to seed — if seed mismatches the file is silently ignored.

static constexpr uint32_t HEIGHTMAP_MAGIC = 0x484D4348; // "HMCH"

void ChunkManager::SaveHeightmapCache(const std::string& path) const {
    if (m_columnMaxCyCache.empty()) return;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return;

    uint32_t count = static_cast<uint32_t>(m_columnMaxCyCache.size());
    uint32_t seed  = m_generator.GetSeed();

    f.write(reinterpret_cast<const char*>(&HEIGHTMAP_MAGIC), 4);
    f.write(reinterpret_cast<const char*>(&seed),  4);
    f.write(reinterpret_cast<const char*>(&count), 4);

    for (const auto& [packed, maxCy] : m_columnMaxCyCache) {
        // Unpack cx/cz from the uint64 key (same packing as PackColumnXZ)
        int32_t cx = static_cast<int32_t>(static_cast<uint32_t>(packed >> 32));
        int32_t cz = static_cast<int32_t>(static_cast<uint32_t>(packed & 0xFFFFFFFF));
        int32_t mc = static_cast<int32_t>(maxCy);
        f.write(reinterpret_cast<const char*>(&cx), 4);
        f.write(reinterpret_cast<const char*>(&cz), 4);
        f.write(reinterpret_cast<const char*>(&mc), 4);
    }
}

void ChunkManager::LoadHeightmapCache(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return;

    auto fileSize = static_cast<size_t>(f.tellg());
    if (fileSize < 12) return;
    f.seekg(0);

    uint32_t magic, seed, count;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&seed),  4);
    f.read(reinterpret_cast<char*>(&count), 4);

    if (magic != HEIGHTMAP_MAGIC) return;
    if (seed  != m_generator.GetSeed()) return;  // stale cache — different seed
    if (fileSize < 12 + static_cast<size_t>(count) * 12) return;  // truncated

    m_columnMaxCyCache.reserve(m_columnMaxCyCache.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t cx, cz, maxCy;
        f.read(reinterpret_cast<char*>(&cx),    4);
        f.read(reinterpret_cast<char*>(&cz),    4);
        f.read(reinterpret_cast<char*>(&maxCy), 4);
        uint64_t key = PackColumnXZ(cx, cz);
        m_columnMaxCyCache.emplace(key, static_cast<int>(maxCy));
    }
}
