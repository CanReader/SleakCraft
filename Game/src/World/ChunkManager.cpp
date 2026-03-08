#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Core/GameObject.hpp>
#include <Core/SceneBase.hpp>
#include <ECS/Components/TransformComponent.hpp>
#include <ECS/Components/MeshComponent.hpp>
#include <ECS/Components/MaterialComponent.hpp>
#include <Runtime/Material.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

ChunkManager::ChunkManager() {}

ChunkManager::~ChunkManager() {
    StopWorkers();
    for (auto& [key, col] : m_columns) {
        if (col.addedToScene && m_scene)
            m_scene->RemoveObject(col.gameObject);
        else
            delete col.gameObject;
    }
    m_columns.clear();
    for (auto& [coord, chunk] : m_chunks)
        delete chunk;
    m_chunks.clear();
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
    int count = static_cast<int>(std::thread::hardware_concurrency()) - 1;
    if (count < 2) count = 2;
    if (count > 6) count = 6;
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
}

void ChunkManager::WorkerThread() {
    while (true) {
        Chunk* chunk = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_taskCV.wait(lock, [this] { return m_shutdown.load() || !m_taskQueue.empty(); });
            if (m_shutdown.load() && m_taskQueue.empty()) return;
            chunk = m_taskQueue.back();
            m_taskQueue.pop_back();
        }
        if (chunk->NeedsGeneration()) {
            m_generator.Generate(chunk);
            chunk->SetNeedsGeneration(false);
        }
        chunk->GenerateMeshData();
        {
            std::lock_guard<std::mutex> lock(m_readyMutex);
            m_readyQueue.push_back(chunk);
        }
    }
}

void ChunkManager::Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material) {
    m_scene = scene;
    m_material = material;
    m_drawDistance = static_cast<float>(m_renderDistance * Chunk::SIZE);
    m_drawDistSq = m_drawDistance * m_drawDistance;
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
    chunk->SetDirty(true);

    chunk->GenerateMeshData();

    std::unordered_set<ColumnKey, ColumnKeyHash> affectedColumns;
    affectedColumns.insert({cx, ChunkYToBand(cy), cz});

    auto rebuildNeighbor = [&](int ncx, int ncy, int ncz) {
        Chunk* neighbor = GetChunk(ncx, ncy, ncz);
        if (neighbor) {
            neighbor->GenerateMeshData();
            affectedColumns.insert({ncx, ChunkYToBand(ncy), ncz});
        }
    };

    if (lx == 0)                rebuildNeighbor(cx - 1, cy, cz);
    if (lx == Chunk::SIZE - 1)  rebuildNeighbor(cx + 1, cy, cz);
    if (ly == 0)                rebuildNeighbor(cx, cy - 1, cz);
    if (ly == Chunk::SIZE - 1)  rebuildNeighbor(cx, cy + 1, cz);
    if (lz == 0)                rebuildNeighbor(cx, cy, cz - 1);
    if (lz == Chunk::SIZE - 1)  rebuildNeighbor(cx, cy, cz + 1);

    for (auto& col : affectedColumns)
        RebuildColumnMesh(col.x, col.yBand, col.z);

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
                    && !m_generator.IsChunkFullySolid(neighbor))
                    neighbor->SetNeedsMeshRebuild(true);
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

void ChunkManager::RebuildColumnMesh(int cx, int yBand, int cz) {
    ColumnKey key{cx, yBand, cz};

    auto colIt = m_columns.find(key);
    if (colIt != m_columns.end()) {
        if (colIt->second.addedToScene)
            m_scene->RemoveObject(colIt->second.gameObject);
        else
            delete colIt->second.gameObject;
        m_columns.erase(colIt);
    }

    int bandMinY = yBand * BAND_SIZE;
    int bandMaxY = bandMinY + BAND_SIZE - 1;

    Sleak::VertexGroup mergedVerts;
    Sleak::IndexGroup mergedIndices;

    for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
        Chunk* chunk = GetChunk(cx, cy, cz);
        if (!chunk || !chunk->HasPendingMesh()) continue;

        auto& md = chunk->GetPendingMeshData();
        if (md.vertices.GetSize() == 0) continue;

        uint32_t baseVertex = static_cast<uint32_t>(mergedVerts.GetSize());

        const Sleak::Vertex* vdata = md.vertices.GetData();
        for (size_t i = 0; i < md.vertices.GetSize(); ++i)
            mergedVerts.AddVertex(vdata[i]);

        const uint32_t* idata = md.indices.GetData();
        for (size_t i = 0; i < md.indices.GetSize(); ++i)
            mergedIndices.add(idata[i] + baseVertex);
    }

    if (mergedVerts.GetSize() == 0) return;

    Sleak::MeshData meshData;
    meshData.vertices = std::move(mergedVerts);
    meshData.indices = std::move(mergedIndices);

    auto* go = new Sleak::GameObject("Column");
    go->AddComponent<Sleak::TransformComponent>(Sleak::Math::Vector3D(0.0f, 0.0f, 0.0f));
    go->AddComponent<Sleak::MaterialComponent>(m_material);
    go->AddComponent<Sleak::MeshComponent>(std::move(meshData));
    go->Initialize();

    m_columns[key] = {go, false};
    m_scene->AddObject(go);
    m_columns[key].addedToScene = true;
}

void ChunkManager::Update(float playerX, float playerY, float playerZ) {
    int centerX = static_cast<int>(std::floor(playerX / Chunk::SIZE));
    int centerY = static_cast<int>(std::floor(playerY / Chunk::SIZE));
    int centerZ = static_cast<int>(std::floor(playerZ / Chunk::SIZE));
    m_lastCenterY = centerY;

    if (centerX != m_lastCenterX || centerZ != m_lastCenterZ) {
        m_lastCenterX = centerX;
        m_lastCenterZ = centerZ;

        // Queue out-of-range chunks for gradual unloading
        for (auto& [coord, chunk] : m_chunks) {
            if (std::abs(coord.x - centerX) > m_renderDistance ||
                std::abs(coord.z - centerZ) > m_renderDistance ||
                coord.y < WorldGenerator::MIN_CHUNK_Y || coord.y > WorldGenerator::MAX_CHUNK_Y) {
                m_pendingUnload.push_back(coord);
            }
        }

        m_pendingLoad.erase(
            std::remove_if(m_pendingLoad.begin(), m_pendingLoad.end(),
                [&](const ChunkCoord& c) {
                    bool out = std::abs(c.x - centerX) > m_renderDistance ||
                               std::abs(c.z - centerZ) > m_renderDistance;
                    if (out) m_pendingSet.erase(c);
                    return out;
                }),
            m_pendingLoad.end());

        // Queue new chunks — iterate XZ then Y column
        for (int cx = centerX - m_renderDistance; cx <= centerX + m_renderDistance; ++cx) {
            for (int cz = centerZ - m_renderDistance; cz <= centerZ + m_renderDistance; ++cz) {
                for (int cy = WorldGenerator::MIN_CHUNK_Y; cy <= WorldGenerator::MAX_CHUNK_Y; ++cy) {
                    ChunkCoord coord{cx, cy, cz};
                    if (m_chunks.count(coord) || m_pendingSet.count(coord)) continue;
                    m_pendingLoad.push_back(coord);
                    m_pendingSet.insert(coord);
                }
            }
        }

        // Sort: farthest first so closest chunks are at back (pop_back is O(1))
        std::sort(m_pendingLoad.begin(), m_pendingLoad.end(),
            [&](const ChunkCoord& a, const ChunkCoord& b) {
                int daXZ = (a.x - centerX) * (a.x - centerX) + (a.z - centerZ) * (a.z - centerZ);
                int dbXZ = (b.x - centerX) * (b.x - centerX) + (b.z - centerZ) * (b.z - centerZ);
                if (daXZ != dbXZ) return daXZ > dbXZ;
                int daY = std::abs(a.y - centerY);
                int dbY = std::abs(b.y - centerY);
                return daY > dbY;
            });
    }

    // Process pending unloads gradually (rate-limited)
    {
        int unloaded = 0;
        std::unordered_set<ColumnKey, ColumnKeyHash> columnsToCheck;
        while (unloaded < m_chunksPerFrame && !m_pendingUnload.empty()) {
            ChunkCoord coord = m_pendingUnload.back();
            m_pendingUnload.pop_back();

            auto it = m_chunks.find(coord);
            if (it == m_chunks.end()) continue;

            Chunk* chunk = it->second;
            if (chunk->IsInFlight() || IsNeighborOfInFlight(coord))
                continue;

            columnsToCheck.insert({coord.x, ChunkYToBand(coord.y), coord.z});
            UnlinkNeighbors(coord, chunk);
            delete chunk;
            m_chunks.erase(it);
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
                auto colIt = m_columns.find(colKey);
                if (colIt != m_columns.end()) {
                    if (colIt->second.addedToScene)
                        m_scene->RemoveObject(colIt->second.gameObject);
                    else
                        delete colIt->second.gameObject;
                    m_columns.erase(colIt);
                }
            }
        }
    }

    if (m_multithreaded) {
        // Phase 1: Process completed chunks from workers — mark bands dirty
        {
            std::vector<Chunk*> ready;
            {
                std::lock_guard<std::mutex> lock(m_readyMutex);
                ready.swap(m_readyQueue);
            }
            for (auto* chunk : ready) {
                chunk->SetInFlight(false);
                if (chunk->HasPendingMesh()) {
                    m_dirtyColumns.insert({chunk->GetChunkX(),
                                           ChunkYToBand(chunk->GetChunkY()),
                                           chunk->GetChunkZ()});
                }
            }
        }

        // Phase 2: Dispatch new chunks to workers
        std::vector<Chunk*> batch;
        int dispatched = 0;
        while (dispatched < m_chunksPerFrame && !m_pendingLoad.empty()) {
            ChunkCoord coord = m_pendingLoad.back();
            m_pendingLoad.pop_back();
            m_pendingSet.erase(coord);

            if (m_chunks.count(coord)) continue;

            // Skip chunks guaranteed to be entirely above terrain
            if (m_generator.IsChunkAboveTerrain(coord.x, coord.y, coord.z)) {
                ++dispatched;
                continue;
            }

            auto* chunk = new Chunk(coord.x, coord.y, coord.z);
            m_chunks[coord] = chunk;
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
            std::lock_guard<std::mutex> lock(m_taskMutex);
            for (auto* chunk : batch) {
                chunk->SetInFlight(true);
                m_taskQueue.push_back(chunk);
            }
        }
        m_taskCV.notify_all();

        // Phase 3: Dispatch remesh requests to workers
        {
            std::vector<Chunk*> remeshBatch;
            int remeshBudget = m_chunksPerFrame;
            for (auto& [c, ch] : m_chunks) {
                if (remeshBudget <= 0) break;
                if (ch->NeedsMeshRebuild() && !ch->IsInFlight()
                    && !ch->NeedsGeneration()) {
                    ch->SetNeedsMeshRebuild(false);
                    remeshBatch.push_back(ch);
                    --remeshBudget;
                }
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
        {
            int rebuilt = 0;
            for (auto it = m_dirtyColumns.begin(); it != m_dirtyColumns.end() && rebuilt < m_uploadsPerFrame; ) {
                RebuildColumnMesh(it->x, it->yBand, it->z);
                it = m_dirtyColumns.erase(it);
                ++rebuilt;
            }
        }
    } else {
        // Synchronous path
        int built = 0;
        std::unordered_set<ColumnKey, ColumnKeyHash> syncDirtyColumns;
        while (built < m_chunksPerFrame && !m_pendingLoad.empty()) {
            ChunkCoord coord = m_pendingLoad.back();
            m_pendingLoad.pop_back();
            m_pendingSet.erase(coord);

            if (m_chunks.count(coord)) continue;

            if (m_generator.IsChunkAboveTerrain(coord.x, coord.y, coord.z)) {
                ++built;
                continue;
            }

            auto* chunk = new Chunk(coord.x, coord.y, coord.z);
            m_chunks[coord] = chunk;
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
        for (auto& [c, ch] : m_chunks) {
            if (rebuilt >= m_chunksPerFrame) break;
            if (ch->NeedsMeshRebuild() && !ch->NeedsGeneration()) {
                ch->SetNeedsMeshRebuild(false);
                ch->GenerateMeshData();
                syncDirtyColumns.insert({c.x, ChunkYToBand(c.y), c.z});
                ++rebuilt;
            }
        }

        for (auto& col : syncDirtyColumns)
            RebuildColumnMesh(col.x, col.yBand, col.z);
    }

    FrustumCull();
}

void ChunkManager::FlushPendingChunks() {
    std::unordered_set<ColumnKey, ColumnKeyHash> flushDirtyColumns;
    while (!m_pendingLoad.empty()) {
        ChunkCoord coord = m_pendingLoad.back();
        m_pendingLoad.pop_back();
        m_pendingSet.erase(coord);

        if (m_chunks.count(coord)) continue;

        if (m_generator.IsChunkAboveTerrain(coord.x, coord.y, coord.z))
            continue;

        auto* chunk = new Chunk(coord.x, coord.y, coord.z);
        m_chunks[coord] = chunk;
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
        flushDirtyColumns.insert({coord.x, ChunkYToBand(coord.y), coord.z});
    }

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
    for (const auto& [coord, chunk] : m_chunks) {
        if (chunk->IsDirty()) {
            result.push_back({coord.x, coord.y, coord.z, chunk->GetBlockData()});
        }
    }
    return result;
}

void ChunkManager::ClearDirtyFlags() {
    for (auto& [coord, chunk] : m_chunks)
        chunk->SetDirty(false);
}

void ChunkManager::LoadChunkData(const std::unordered_map<int64_t, std::array<uint8_t, 4096>>& data) {
    m_savedBlockData = data;
}

void ChunkManager::ForceReload() {
    bool wasMultithreaded = m_multithreaded;
    if (wasMultithreaded) StopWorkers();

    // Remove all column meshes
    for (auto& [key, col] : m_columns) {
        if (col.gameObject && col.addedToScene)
            m_scene->RemoveObject(col.gameObject);
        else
            delete col.gameObject;
    }
    m_columns.clear();
    m_dirtyColumns.clear();

    // Remove all chunks
    for (auto& [coord, chunk] : m_chunks) {
        delete chunk;
    }
    m_chunks.clear();
    m_pendingLoad.clear();
    m_pendingSet.clear();
    m_lastCenterX = INT_MAX;
    m_lastCenterY = INT_MAX;
    m_lastCenterZ = INT_MAX;

    if (wasMultithreaded) StartWorkers();
}

void ChunkManager::FrustumCull() const {
    const auto& frustum = Sleak::Camera::GetMainViewFrustum();
    const auto& camPos = Sleak::Camera::GetMainCameraPosition();
    float camX = camPos.GetX();
    float camY = camPos.GetY();
    float camZ = camPos.GetZ();

    for (auto& [key, col] : m_columns) {
        auto* go = col.gameObject;
        if (!go || !col.addedToScene) continue;

        float minX = static_cast<float>(key.x * Chunk::SIZE);
        float minY = static_cast<float>(key.yBand * BAND_SIZE * Chunk::SIZE);
        float minZ = static_cast<float>(key.z * Chunk::SIZE);
        float maxX = minX + Chunk::SIZE;
        float maxY = minY + BAND_SIZE * Chunk::SIZE;
        float maxZ = minZ + Chunk::SIZE;

        // 3D distance check
        float dx = (camX < minX) ? (minX - camX) : (camX > maxX) ? (camX - maxX) : 0.0f;
        float dy = (camY < minY) ? (minY - camY) : (camY > maxY) ? (camY - maxY) : 0.0f;
        float dz = (camZ < minZ) ? (minZ - camZ) : (camZ > maxZ) ? (camZ - maxZ) : 0.0f;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq > m_drawDistSq) {
            go->SetActive(false);
            continue;
        }

        bool visible = frustum.IsAABBVisible(
            Sleak::Math::Vector3D(minX, minY, minZ),
            Sleak::Math::Vector3D(maxX, maxY, maxZ));

        go->SetActive(visible);
    }
}
