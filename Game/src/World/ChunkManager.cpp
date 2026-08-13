#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Core/SceneBase.hpp>
#include <Culling/CullingSystem.hpp>
#include <Core/Logger.hpp>
#include <Runtime/Material.hpp>
#include <Runtime/MeshBatch.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
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

    // Release in-flight tokens or the chunks can never unload/remesh again
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        for (Chunk* c : m_taskQueue) c->SetInFlight(false);
        m_taskQueue.clear();
    }
    // Workers drain the task queue before exiting; requeue their results
    {
        std::lock_guard<std::mutex> lock(m_readyMutex);
        for (Chunk* c : m_readyQueue) {
            c->SetInFlight(false);
            c->SetNeedsMeshRebuild(true);
            m_chunksNeedingRemesh.insert(
                {c->GetChunkX(), c->GetChunkY(), c->GetChunkZ()});
        }
        m_readyQueue.clear();
    }
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

void ChunkManager::SetRenderDistance(int chunks) {
    if (chunks == m_renderDistance) return;
    int oldRD = m_renderDistance;
    SLEAK_INFO("Render distance changed: {} -> {}", oldRD, chunks);
    m_renderDistance = chunks;
    m_drawDistance = static_cast<float>(chunks * Chunk::SIZE);
    m_drawDistSq = m_drawDistance * m_drawDistance;

    if (chunks > oldRD) {
        m_pendingUnload.clear();
    } else {
        // RD decreased — immediately free out-of-range column meshes and
        // chunks so VRAM is released before new allocations begin.
        int cx = (m_lastCenterX == INT_MAX) ? 0 : m_lastCenterX;
        int cz = (m_lastCenterZ == INT_MAX) ? 0 : m_lastCenterZ;

        // Erase column meshes outside new range first (frees GPU buffers)
        for (auto it = m_columns.begin(); it != m_columns.end(); ) {
            if (std::abs(it->first.x - cx) > m_renderDistance ||
                std::abs(it->first.z - cz) > m_renderDistance) {
                m_dirtyColumns.erase(it->first);
                it = m_columns.erase(it);
            } else {
                ++it;
            }
        }

        // Synchronously unload all out-of-range chunks
        m_pendingUnload.clear();
        std::vector<Chunk*> toDelete;
        for (Chunk* chunk : m_activeChunks) {
            if (!chunk) continue;
            if (std::abs(chunk->GetChunkX() - cx) > m_renderDistance ||
                std::abs(chunk->GetChunkZ() - cz) > m_renderDistance) {
                // In-flight neighbors still dereference us — defer to
                // the gradual unloader (respiral below re-detects these)
                ChunkCoord coord{chunk->GetChunkX(), chunk->GetChunkY(),
                                 chunk->GetChunkZ()};
                if (!chunk->IsInFlight() && !IsNeighborOfInFlight(coord))
                    toDelete.push_back(chunk);
            }
        }
        for (Chunk* chunk : toDelete) {
            UnlinkNeighbors({chunk->GetChunkX(), chunk->GetChunkY(), chunk->GetChunkZ()}, chunk);
            ForceUnloadChunk(chunk);
            delete chunk;
        }
    }

    m_lastCenterX = INT_MAX;
    BuildLoadSpiral();
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
    int requiredWidth = (m_renderDistance + 2) * 2;
    bool needsRegrid = requiredWidth > m_gridWidth;

    if (needsRegrid) {
        m_gridWidth = requiredWidth;
        int totalSize = m_gridWidth * m_gridWidth * m_gridHeight;
        m_chunkGrid.assign(totalSize, nullptr);

        for (Chunk* chunk : m_activeChunks) {
            if (!chunk) continue;
            int idx = GetGridIndex(chunk->GetChunkX(), chunk->GetChunkY(), chunk->GetChunkZ());
            if (idx >= 0) m_chunkGrid[idx] = chunk;
        }
    }

    m_loadSpiral.clear();
    for (int x = -m_renderDistance; x <= m_renderDistance; ++x) {
        for (int z = -m_renderDistance; z <= m_renderDistance; ++z) {
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
    // Worker may still be writing blocks during generation — treat as air
    if (chunk->NeedsGeneration()) return BlockType::Air;

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

    // Defer edit while a worker may read/write this chunk or a neighbor
    if (chunk->IsInFlight() || chunk->NeedsGeneration() ||
        IsNeighborOfInFlight({cx, cy, cz})) {
        m_pendingEdits.push_back({worldX, worldY, worldZ, type});
        return true;
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

void ChunkManager::FlushPendingEdits() {
    // Apply deferred edits for save/exit; visuals refresh via remesh
    std::vector<PendingBlockEdit> edits;
    edits.swap(m_pendingEdits);
    for (auto& e : edits) {
        int cx = floorDiv(e.x, Chunk::SIZE);
        int cy = floorDiv(e.y, Chunk::SIZE);
        int cz = floorDiv(e.z, Chunk::SIZE);
        Chunk* chunk = GetChunk(cx, cy, cz);
        if (!chunk) continue;
        // Still owned by a worker — keep deferred, lands in the next save
        if (chunk->IsInFlight() || chunk->NeedsGeneration() ||
            IsNeighborOfInFlight({cx, cy, cz})) {
            m_pendingEdits.push_back(e);
            continue;
        }
        chunk->SetBlock(floorMod(e.x, Chunk::SIZE), floorMod(e.y, Chunk::SIZE),
                        floorMod(e.z, Chunk::SIZE), e.type);
        chunk->SetDirty(true);
        chunk->SetNeedsMeshRebuild(true);
        m_chunksNeedingRemesh.insert({cx, cy, cz});
    }
}

VoxelRaycastResult ChunkManager::VoxelRaycast(
    const Sleak::Math::Vector3D& origin,
    const Sleak::Math::Vector3D& direction,
    float maxDist) const
{
    return m_queries.VoxelRaycast(origin, direction, maxDist);
}

VoxelCollisionResult ChunkManager::ResolveVoxelCollision(
    const Sleak::Math::Vector3D& eyePos,
    float halfWidth, float height, float eyeOffset) const
{
    return m_queries.ResolveVoxelCollision(eyePos, halfWidth, height, eyeOffset);
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

    VoxelVertexBuffer mergedVerts;
    Sleak::IndexGroup mergedIndices;
    VoxelVertexBuffer mergedWaterVerts;
    Sleak::IndexGroup mergedWaterIndices;

    // Dispatch ALL consumed-mesh siblings in one batch so workers re-mesh
    // the band in parallel; dispatching one per attempt serialized a column
    // refresh across ~8 frames.
    if (m_multithreaded && allowDefer) {
        std::vector<Chunk*> stale;
        for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
            Chunk* chunk = GetChunk(cx, cy, cz);
            if (!chunk || chunk->IsInFlight() || chunk->NeedsGeneration())
                continue;
            if (!chunk->HasPendingMesh()) stale.push_back(chunk);
        }
        if (!stale.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_taskMutex);
                for (auto* ch : stale) {
                    ch->SetInFlight(true);
                    m_taskQueue.push_back(ch);
                }
            }
            m_taskCV.notify_all();
            m_dirtyColumns.insert(key);
            return;
        }
    }

    // Exact mesh Y extents — tight bounds are what let occlusion culling
    // reject columns whose full-band AABB would always poke into the sky.
    float meshMinY = 1e30f;
    float meshMaxY = -1e30f;

    for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
        Chunk* chunk = GetChunk(cx, cy, cz);
        // Skip chunks not ready (in-flight or not yet generated)
        if (!chunk || chunk->IsInFlight() || chunk->NeedsGeneration()) continue;

        // Mesh data was consumed — regenerate it (sync fallback).
        // A worker may be generating a neighbor: re-queue instead of racing.
        if (!chunk->HasPendingMesh()) {
            if (IsNeighborOfInFlight({cx, cy, cz})) {
                m_dirtyColumns.insert(key);
                continue;
            }
            chunk->GenerateMeshData();
        }

        // Merge opaque mesh
        {
            auto& md = chunk->GetPendingMeshData();
            chunk->ClearPendingMesh();

            if (md.vertices.GetSize() > 0) {
                uint32_t baseVertex = static_cast<uint32_t>(mergedVerts.GetSize());
                const ::VoxelVertex* vdata = md.vertices.GetData();
                for (size_t i = 0; i < md.vertices.GetSize(); ++i) {
                    mergedVerts.AddVertex(vdata[i]);
                    if (vdata[i].py < meshMinY) meshMinY = vdata[i].py;
                    if (vdata[i].py > meshMaxY) meshMaxY = vdata[i].py;
                }
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
                const ::VoxelVertex* vdata = wd.vertices.GetData();
                for (size_t i = 0; i < wd.vertices.GetSize(); ++i) {
                    mergedWaterVerts.AddVertex(vdata[i]);
                    if (vdata[i].py < meshMinY) meshMinY = vdata[i].py;
                    if (vdata[i].py > meshMaxY) meshMaxY = vdata[i].py;
                }
                const uint32_t* idata = wd.indices.GetData();
                for (size_t i = 0; i < wd.indices.GetSize(); ++i)
                    mergedWaterIndices.add(idata[i] + baseVertex);
            }
            wd.vertices.release();
            wd.indices.release();
        }
    }

    if (mergedVerts.GetSize() == 0 && mergedWaterVerts.GetSize() == 0) {
        m_columns.erase(key);
        return;
    }

    // Release old GPU buffers BEFORE allocating new ones to reduce peak VRAM.
    auto existingIt = m_columns.find(key);
    if (existingIt != m_columns.end()) {
        existingIt->second.mesh = {};
        existingIt->second.waterMesh = {};
    }

    ColumnMesh col;
    if (mergedVerts.GetSize() > 0)
        col.mesh = Sleak::MeshBatch::CreateMesh(
            GetVoxelVertexFormat(), mergedVerts.GetData(),
            mergedVerts.GetSizeInBytes(), mergedIndices.GetData(),
            mergedIndices.GetSize());
    if (mergedWaterVerts.GetSize() > 0)
        col.waterMesh = Sleak::MeshBatch::CreateMesh(
            GetVoxelVertexFormat(), mergedWaterVerts.GetData(),
            mergedWaterVerts.GetSizeInBytes(), mergedWaterIndices.GetData(),
            mergedWaterIndices.GetSize());

    if (!col.mesh.IsValid() && !col.waterMesh.IsValid()) {
        m_oomThisFrame = true;
        return;
    }

    // Column world AABB (16x16 XZ footprint, exact mesh Y extent).
    float minX = static_cast<float>(cx * Chunk::SIZE);
    float minZ = static_cast<float>(cz * Chunk::SIZE);
    float footMinY = meshMinY;
    float footMaxY = meshMaxY;
    col.bounds = Sleak::Math::AABB(
        Sleak::Math::Vector3D(minX, footMinY, minZ),
        Sleak::Math::Vector3D(minX + Chunk::SIZE, footMaxY,
                              minZ + Chunk::SIZE));

    // Occluder boxes: per 8x8 quadrant, merge contiguous opaque runs
    // across chunk boundaries.
    col.occluders.clear();
    for (int q = 0; q < 4; ++q) {
        float qMinX = minX + (q & 1) * 8.0f;
        float qMinZ = minZ + ((q >> 1) & 1) * 8.0f;
        bool haveRun = false;
        float accMinY = 0.0f, accMaxY = 0.0f;
        auto flush = [&]() {
            Sleak::Math::AABB box(
                Sleak::Math::Vector3D(qMinX, accMinY, qMinZ),
                Sleak::Math::Vector3D(qMinX + 8.0f, accMaxY, qMinZ + 8.0f));
            box.Expand(-0.05f);
            col.occluders.push_back(box);
        };
        for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
            Chunk* chunk = GetChunk(cx, cy, cz);
            if (!chunk || chunk->IsInFlight() || chunk->NeedsGeneration())
                continue;
            int8_t rMin[Chunk::OCC_MAX_RUNS];
            int8_t rMax[Chunk::OCC_MAX_RUNS];
            int runs = chunk->GetOccluderRuns(q, rMin, rMax);
            int base = cy * Chunk::SIZE;
            for (int i = 0; i < runs; ++i) {
                float wMin = static_cast<float>(base + rMin[i]);
                float wMax = static_cast<float>(base + rMax[i] + 1);
                if (haveRun && wMin == accMaxY) {
                    accMaxY = wMax;
                } else {
                    if (haveRun) flush();
                    accMinY = wMin;
                    accMaxY = wMax;
                    haveRun = true;
                }
            }
        }
        if (haveRun) flush();
    }

    col.visible = true;
    m_columns[key] = std::move(col);
}

void ChunkManager::StashIfDirty(Chunk* chunk) {
    // Preserve unsaved edits across unload; reload paths restore + re-dirty
    if (!chunk || !chunk->IsDirty()) return;
    ChunkCoord c{chunk->GetChunkX(), chunk->GetChunkY(), chunk->GetChunkZ()};
    std::memcpy(m_savedBlockData[PackCoord(c.x, c.y, c.z)].data(),
                chunk->GetBlockData(), 4096);
    m_stashedDirty.insert(c);
}

void ChunkManager::ForceUnloadChunk(Chunk* chunk) {
    if (!chunk) return;
    StashIfDirty(chunk);
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
    // Retry deferred block edits; SetBlockAt re-queues any still blocked
    if (!m_pendingEdits.empty()) {
        std::vector<PendingBlockEdit> retry;
        retry.swap(m_pendingEdits);
        for (auto& e : retry) SetBlockAt(e.x, e.y, e.z, e.type);
    }

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
        // Over-cap safety valve: drain the backlog faster than it grows
        int unloadBudget = m_chunksPerFrame;
        if (static_cast<int>(m_columns.size()) > MAX_COLUMN_MESHES)
            unloadBudget *= 4;
        std::unordered_set<ColumnKey, ColumnKeyHash> columnsToCheck;
        std::vector<ChunkCoord> deferredUnloads;
        while (unloaded < unloadBudget && !m_pendingUnload.empty()) {
            ChunkCoord coord = m_pendingUnload.back();
            m_pendingUnload.pop_back();

            Chunk* chunk = GetChunk(coord.x, coord.y, coord.z);
            if (!chunk) continue;

            if (std::abs(coord.x - centerX) <= m_renderDistance &&
                std::abs(coord.z - centerZ) <= m_renderDistance)
                continue;

            // Busy — RE-QUEUE for next frame, never drop (VRAM leak otherwise).
            // Charge the budget so a deferred backlog can't grow the scan
            // unbounded within one frame.
            if (chunk->IsInFlight() || IsNeighborOfInFlight(coord)) {
                deferredUnloads.push_back(coord);
                ++unloaded;
                continue;
            }

            columnsToCheck.insert({coord.x, ChunkYToBand(coord.y), coord.z});
            UnlinkNeighbors(coord, chunk);
            ForceUnloadChunk(chunk);
            delete chunk;
            ++unloaded;
        }
        // Front insert: busy entries retry AFTER fresh ones, not before
        m_pendingUnload.insert(m_pendingUnload.begin(), deferredUnloads.begin(),
                               deferredUnloads.end());

        // Free column meshes whose bands lost all chunks.  For columns
        // that still have SOME chunks, erase the column mesh immediately
        // (it references stale data) rather than keeping the oversized
        // buffer alive until the next dirty-column rebuild.  The column
        // will be re-created when the remaining chunks are remeshed.
        for (auto& colKey : columnsToCheck) {
            bool hasChunks = false;
            int bandMinY = colKey.yBand * BAND_SIZE;
            int bandMaxY = bandMinY + BAND_SIZE - 1;
            for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
                if (GetChunk(colKey.x, cy, colKey.z)) { hasChunks = true; break; }
            }
            if (!hasChunks) {
                m_columns.erase(colKey);
                m_dirtyColumns.erase(colKey);
            } else {
                // Column still has chunks but lost some — the existing
                // column mesh is oversized.  Free its GPU buffers now
                // and queue a rebuild so the next pass allocates a
                // smaller buffer.
                auto it = m_columns.find(colKey);
                if (it != m_columns.end()) {
                    it->second.mesh = {};
                    it->second.waterMesh = {};
                }
                m_dirtyColumns.insert(colKey);
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
        int dispatchBudget = m_dispatchPerFrame;

        std::vector<Chunk*> batch;
        std::vector<ChunkCoord> deferredLoads;
        int dispatched = 0;
        while (dispatched < dispatchBudget && !m_pendingLoad.empty()) {
            ChunkCoord coord = m_pendingLoad.back();
            m_pendingLoad.pop_back();

            if (GetChunk(coord.x, coord.y, coord.z)) continue;

            int idx = GetGridIndex(coord.x, coord.y, coord.z);
            // Aliased slot occupant still referenced by a worker — retry later
            if (idx >= 0 && m_chunkGrid[idx] != nullptr) {
                Chunk* stale = m_chunkGrid[idx];
                ChunkCoord staleCoord{stale->GetChunkX(), stale->GetChunkY(),
                                      stale->GetChunkZ()};
                if (stale->IsInFlight() || IsNeighborOfInFlight(staleCoord)) {
                    deferredLoads.push_back(coord);
                    continue;
                }
                UnlinkNeighbors(staleCoord, stale);
                ForceUnloadChunk(stale);
                delete stale;
            }

            auto* chunk = new Chunk(coord.x, coord.y, coord.z);
            if (idx >= 0) {
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
                if (m_stashedDirty.erase(coord)) chunk->SetDirty(true);
            }
            LinkNeighbors(coord, chunk);
            batch.push_back(chunk);
            ++dispatched;
        }
        m_pendingLoad.insert(m_pendingLoad.end(), deferredLoads.begin(),
                             deferredLoads.end());

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
        {
            m_oomThisFrame = false;
            // Strict cap: never exceed m_uploadsPerFrame column rebuilds
            // per frame.  The old adaptive *2 boost caused VRAM spikes
            // when many columns became dirty simultaneously (e.g. after
            // a render-distance change or fast player movement).
            int uploadBudget = m_uploadsPerFrame;

            std::vector<ColumnKey> toRebuild;
            {
                int count = 0;
                for (auto it = m_dirtyColumns.begin();
                     it != m_dirtyColumns.end() && count < uploadBudget; ) {
                    bool anyInFlight = false;
                    int bandMinY = it->yBand * BAND_SIZE;
                    int bandMaxY = bandMinY + BAND_SIZE - 1;
                    for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
                        Chunk* ch = GetChunk(it->x, cy, it->z);
                        if (ch && ch->IsInFlight()) { anyInFlight = true; break; }
                    }
                    if (anyInFlight) {
                        ++it;
                        continue;
                    }
                    toRebuild.push_back(*it);
                    it = m_dirtyColumns.erase(it);
                    ++count;
                }
            }

            for (auto& key : toRebuild) {
                if (m_oomThisFrame) {
                    m_dirtyColumns.insert(key);
                    continue;
                }
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

            int idx = GetGridIndex(coord.x, coord.y, coord.z);
            // Guard against stragglers from a prior multithreaded phase
            if (idx >= 0 && m_chunkGrid[idx] != nullptr) {
                Chunk* stale = m_chunkGrid[idx];
                ChunkCoord staleCoord{stale->GetChunkX(), stale->GetChunkY(),
                                      stale->GetChunkZ()};
                if (stale->IsInFlight() || IsNeighborOfInFlight(staleCoord))
                    continue;
                UnlinkNeighbors(staleCoord, stale);
                ForceUnloadChunk(stale);
                delete stale;
            }

            auto* chunk = new Chunk(coord.x, coord.y, coord.z);
            if (idx >= 0) {
                m_chunkGrid[idx] = chunk;
                chunk->SetActiveIndex(static_cast<int>(m_activeChunks.size()));
                m_activeChunks.push_back(chunk);
            }
            int64_t key = PackCoord(coord.x, coord.y, coord.z);
            auto savedIt = m_savedBlockData.find(key);
            if (savedIt != m_savedBlockData.end()) {
                std::memcpy(const_cast<uint8_t*>(chunk->GetBlockData()),
                            savedIt->second.data(), 4096);
                if (m_stashedDirty.erase(coord)) chunk->SetDirty(true);
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

        m_oomThisFrame = false;
        for (auto& col : syncDirtyColumns) {
            if (m_oomThisFrame) {
                m_dirtyColumns.insert(col);
                continue;
            }
            RebuildColumnMesh(col.x, col.yBand, col.z);
        }
    }

    UpdateVisibility();
}

void ChunkManager::FlushPendingChunks() {
    // Bulk synchronous load: drain workers first — the unguarded deletes
    // and meshing below must not run concurrently with generation.
    bool restartWorkers = m_multithreaded && !m_workers.empty();
    if (restartWorkers) StopWorkers();

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
            if (m_stashedDirty.erase(coord)) chunk->SetDirty(true);
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

    if (restartWorkers) StartWorkers();
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
    // Edited chunks that were unloaded before this save
    for (const auto& c : m_stashedDirty) {
        auto it = m_savedBlockData.find(PackCoord(c.x, c.y, c.z));
        if (it != m_savedBlockData.end())
            result.push_back({c.x, c.y, c.z, it->second.data()});
    }
    return result;
}

void ChunkManager::ClearDirtyFlags() {
    for (Chunk* chunk : m_activeChunks)
        if (chunk) chunk->SetDirty(false);
    m_stashedDirty.clear();
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

void ChunkManager::UpdateVisibility() {
    const auto& camPos = Sleak::Camera::GetMainCameraPosition();
    float camX = camPos.GetX();
    float camZ = camPos.GetZ();

    // Force-render columns near the player regardless of camera frustum, so
    // terrain above caves/enclosed spaces stays in the shadow map.
    constexpr float SHADOW_FORCE_DIST = 48.0f;

    // Pass A: distance cull, shadow flag, occluder submission.
    m_cullCandidates.clear();
    for (auto& [key, col] : m_columns) {
        if (!col.mesh.IsValid() && !col.waterMesh.IsValid()) {
            col.visible = false;
            continue;
        }

        float minX = col.bounds.min.GetX();
        float maxX = col.bounds.max.GetX();
        float minZ = col.bounds.min.GetZ();
        float maxZ = col.bounds.max.GetZ();

        // Horizontal-only distance check (XZ cylinder) so columns stay visible
        // when the player is high above the terrain
        float dx = (camX < minX) ? (minX - camX) : (camX > maxX) ? (camX - maxX) : 0.0f;
        float dz = (camZ < minZ) ? (minZ - camZ) : (camZ > maxZ) ? (camZ - maxZ) : 0.0f;
        col.distSq = dx * dx + dz * dz;

        // Shadow-caster cull: only columns within the configured caster
        // distance render into the shadow map.
        col.castsShadow = (col.distSq <= m_shadowCasterDistSq);

        if (col.distSq > m_drawDistSq) {
            col.visible = false;
            continue;
        }

        // Near columns act as occluders — beyond half draw distance an
        // occluder hides almost nothing but still costs raster time.
        float occDist = m_drawDistance * 0.5f;
        if (col.distSq <= occDist * occDist) {
            for (const auto& occ : col.occluders)
                Sleak::CullingSystem::SubmitOccluderBox(occ);
        }

        if (col.distSq <= SHADOW_FORCE_DIST * SHADOW_FORCE_DIST) {
            col.visible = true;  // force-visible, skip occlusion test
            continue;
        }

        col.visible = false;  // decided in Pass B
        m_cullCandidates.push_back(&col);
    }

    Sleak::CullingSystem::FinalizeOccluders();

    // Pass B: frustum + occlusion query for remaining candidates.
    for (ColumnMesh* col : m_cullCandidates)
        col->visible = Sleak::CullingSystem::IsVisible(col->bounds);
}

void ChunkManager::SetCullingEnabled(bool frustum, bool occlusion) {
    Sleak::CullingSystem::SetFrustumCullingEnabled(frustum);
    Sleak::CullingSystem::SetOcclusionCullingEnabled(occlusion);
}

void ChunkManager::RenderColumns() {
    // Front-to-back draw order for better early-z / less overdraw.
    m_renderScratch.clear();
    for (auto& [key, col] : m_columns)
        if (col.visible && col.mesh.IsValid())
            m_renderScratch.push_back(&col);
    std::sort(m_renderScratch.begin(), m_renderScratch.end(),
              [](const ColumnMesh* a, const ColumnMesh* b) {
                  return a->distSq < b->distSq;
              });

    Sleak::MeshBatch::BeginBatch(m_material.get());
    for (ColumnMesh* col : m_renderScratch)
        Sleak::MeshBatch::Draw(col->mesh, col->castsShadow);
    Sleak::MeshBatch::EndBatch();
}

void ChunkManager::RenderWater() {
    if (!m_waterMaterial) return;
    // Back-to-front draw order for correct transparency.
    m_waterScratch.clear();
    for (auto& [key, col] : m_columns)
        if (col.visible && col.waterMesh.IsValid())
            m_waterScratch.push_back(&col);
    std::sort(m_waterScratch.begin(), m_waterScratch.end(),
              [](const ColumnMesh* a, const ColumnMesh* b) {
                  return a->distSq > b->distSq;
              });

    // Water never casts shadows — keeps ~200 meshes out of the shadow map.
    Sleak::MeshBatch::BeginBatch(m_waterMaterial.get());
    for (ColumnMesh* col : m_waterScratch)
        Sleak::MeshBatch::Draw(col->waterMesh, false);
    Sleak::MeshBatch::EndBatch();
}

void ChunkManager::SaveHeightmapCache(const std::string& path) const {
    HeightmapCache::Save(path, m_columnMaxCyCache, m_generator.GetSeed());
}

void ChunkManager::LoadHeightmapCache(const std::string& path) {
    HeightmapCache::Load(path, m_columnMaxCyCache, m_generator.GetSeed());
}
