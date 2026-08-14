#include "World/ChunkMesher.hpp"
#include "World/ChunkManager.hpp"
#include <Runtime/Material.hpp>
#include <Runtime/MeshBatch.hpp>
#include <mutex>
#include <vector>

void ChunkMesher::RebuildColumnMesh(int cx, int yBand, int cz, bool allowDefer) {
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
        m_mgr.GetColumns().erase(key);
        return;
    }

    // Release old GPU buffers BEFORE allocating new ones to reduce peak VRAM.
    auto existingIt = m_mgr.GetColumns().find(key);
    if (existingIt != m_mgr.GetColumns().end()) {
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
        m_mgr.SetOOMThisFrame(true);
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
    m_mgr.GetColumns()[key] = std::move(col);
}
