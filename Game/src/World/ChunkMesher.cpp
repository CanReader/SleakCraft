#include "World/ChunkMesher.hpp"
#include "World/ChunkManager.hpp"
#include "World/ChunkVisibilityGraph.hpp"
#include "World/QuadIndexBuffer.hpp"
#include <Runtime/Material.hpp>
#include <Runtime/MeshBatch.hpp>
#include <mutex>
#include <vector>

void ChunkMesher::RebuildColumnMesh(int cx, int yBand, int cz, bool allowDefer) {
    ColumnKey key{cx, yBand, cz};

    int bandMinY = yBand * BAND_SIZE;
    int bandMaxY = bandMinY + BAND_SIZE - 1;

    VoxelVertexBuffer& mergedVerts = m_mergedVerts;
    mergedVerts.clear();

    // Dispatch ALL consumed-mesh siblings in one batch so workers re-mesh
    // the band in parallel; dispatching one per attempt serialized a column
    // refresh across ~8 frames.
    if (m_mgr.IsMultithreaded() && allowDefer) {
        std::vector<Chunk*> stale;
        for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
            Chunk* chunk = m_mgr.GetChunk(cx, cy, cz);
            if (!chunk || chunk->IsInFlight() || chunk->NeedsGeneration())
                continue;
            if (!chunk->HasPendingMesh()) stale.push_back(chunk);
        }
        if (!stale.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_mgr.GetTaskMutex());
                for (auto* ch : stale) {
                    ch->SetInFlight(true);
                    m_mgr.GetTaskQueue().push_back(ch);
                }
            }
            m_mgr.GetTaskCV().notify_all();
            m_mgr.GetDirtyColumns().insert(key);
            return;
        }
    }

    // Exact mesh Y extents — tight bounds are what let occlusion culling
    // reject columns whose full-band AABB would always poke into the sky.
    float meshMinY = 1e30f;
    float meshMaxY = -1e30f;

    // Collect ready chunks by band slot so the merged buffer is sized once
    Chunk* ready[COLUMN_CHUNKS] = {};
    ChunkVis vis[COLUMN_CHUNKS];
    size_t vertexTotal = 0;
    for (int cy = bandMinY; cy <= bandMaxY; ++cy) {
        Chunk* chunk = m_mgr.GetChunk(cx, cy, cz);
        // Skip chunks not ready (in-flight or not yet generated)
        if (!chunk || chunk->IsInFlight() || chunk->NeedsGeneration()) continue;

        // Mesh data was consumed — regenerate it (sync fallback).
        // A worker may be generating a neighbor: re-queue instead of racing.
        if (!chunk->HasPendingMesh()) {
            if (m_mgr.IsNeighborOfInFlight({cx, cy, cz})) {
                m_mgr.GetDirtyColumns().insert(key);
                continue;
            }
            chunk->GenerateMeshData();
        }

        ready[cy - bandMinY] = chunk;
        const ChunkMeshData& md = chunk->GetPendingMeshData();
        vertexTotal += md.vertices.GetSize();
        if (md.vertices.GetSize() > 0) {
            if (md.minY < meshMinY) meshMinY = md.minY;
            if (md.maxY > meshMaxY) meshMaxY = md.maxY;
            vis[cy - bandMinY].minY = md.minY;
            vis[cy - bandMinY].maxY = md.maxY;
            vis[cy - bandMinY].hasQuads = true;
        }
    }

    // Covered water, top down from the open sky above the band
    static_assert(COLUMN_CHUNKS == ChunkVisibilityGraph::WORLD_LAYERS);
    Chunk::Columns openAbove;
    openAbove.set();
    for (int k = COLUMN_CHUNKS - 1; k >= 0; --k) {
        // Unknown chunks pass open on: a superset of the true links
        if (!ready[k]) {
            openAbove.set();
            continue;
        }
        const Chunk::Visibility& chunkVis = ready[k]->GetVisibility();
        vis[k].seeThrough = chunkVis.seeThrough;
        vis[k].covered =
            ResolveCoveredWater(chunkVis, openAbove, vis[k].openWater);
    }

    // Where each (stream, group) segment starts in its chunk's vector
    uint32_t segStart[COLUMN_CHUNKS][VOXEL_STREAM_COUNT][VOXEL_DIR_GROUPS];
    for (int k = 0; k < COLUMN_CHUNKS; ++k) {
        if (!ready[k]) continue;
        uint32_t quad = 0;
        for (int s = 0; s < VOXEL_STREAM_COUNT; ++s) {
            for (int g = 0; g < VOXEL_DIR_GROUPS; ++g) {
                segStart[k][s][g] = quad;
                quad += ready[k]->GetPendingMeshData().quads[s][g];
            }
        }
    }

    // One buffer: streams, then groups, then chunk slots
    StreamRanges ranges[VOXEL_STREAM_COUNT];
    mergedVerts.Reserve(vertexTotal);
    for (int s = 0; s < VOXEL_STREAM_COUNT; ++s) {
        for (int g = 0; g < VOXEL_DIR_GROUPS; ++g) {
            for (int slot = 0; slot < COLUMN_CHUNKS; ++slot) {
                ranges[s].start[g * COLUMN_CHUNKS + slot] =
                    static_cast<uint32_t>(mergedVerts.GetSize() /
                                          VOXEL_QUAD_CORNERS);
                const int k = SlotChunk(g, slot);
                if (!ready[k]) continue;
                const ChunkMeshData& md = ready[k]->GetPendingMeshData();
                mergedVerts.Append(md.vertices.GetData() +
                                       segStart[k][s][g] * VOXEL_QUAD_CORNERS,
                                   md.quads[s][g] * VOXEL_QUAD_CORNERS);
            }
        }
        ranges[s].start[VOXEL_DIR_GROUPS * COLUMN_CHUNKS] =
            static_cast<uint32_t>(mergedVerts.GetSize() / VOXEL_QUAD_CORNERS);
    }

    for (Chunk* chunk : ready) {
        if (!chunk) continue;
        chunk->ClearPendingMesh();
        chunk->GetPendingMeshData().vertices.release();
    }

    // Edits re-run the visibility graph next frame, streaming within a few
    m_mgr.MarkGraphDirty(!allowDefer);
    if (mergedVerts.GetSize() == 0) {
        m_mgr.EraseColumn(key);
        return;
    }

    // Release old GPU buffers BEFORE allocating new ones to reduce peak VRAM.
    auto existingIt = m_mgr.GetColumns().find(key);
    if (existingIt != m_mgr.GetColumns().end()) existingIt->second.mesh = {};

    Sleak::MeshHandle mesh = QuadIndexBuffer::CreateMesh(mergedVerts);
    if (!mesh.IsValid()) {
        m_mgr.SetOOMThisFrame(true);
        return;
    }

    ColumnMesh& col = existingIt != m_mgr.GetColumns().end()
                          ? existingIt->second
                          : m_mgr.AddColumn(key);
    col.mesh = std::move(mesh);
    for (int s = 0; s < VOXEL_STREAM_COUNT; ++s) col.ranges[s] = ranges[s];
    for (int k = 0; k < COLUMN_CHUNKS; ++k) col.chunks[k] = vis[k];
    col.reachableChunks = ALL_COLUMN_CHUNKS;
    // Every generated chunk of a column holds the same noise surface
    for (Chunk* chunk : ready)
        if (chunk && chunk->GetMinSurface() != Chunk::NO_SURFACE)
            col.minSurface = chunk->GetMinSurface();

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
            Chunk* chunk = m_mgr.GetChunk(cx, cy, cz);
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
}
