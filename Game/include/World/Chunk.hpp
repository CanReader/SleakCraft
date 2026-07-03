#ifndef _CHUNK_HPP_
#define _CHUNK_HPP_

#include "Block.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <Runtime/MeshData.hpp>
#include <Memory/RefPtr.h>

namespace Sleak {
    class Material;
    class GameObject;
    class SceneBase;
}

struct ChunkMeshData {
    Sleak::VoxelVertexGroup vertices;
    Sleak::IndexGroup indices;
};

class Chunk {
public:
    static constexpr int SIZE = 16;
    static constexpr int VOLUME = SIZE * SIZE * SIZE;

    Chunk(int cx, int cy, int cz);
    ~Chunk();

    void SetBlock(int x, int y, int z, BlockType type);
    BlockType GetBlock(int x, int y, int z) const;

    void SetNeighbor(BlockFace face, Chunk* chunk);

    void BuildMesh(const Sleak::RefPtr<Sleak::Material>& material);
    void GenerateMeshData();
    void UploadMesh(const Sleak::RefPtr<Sleak::Material>& material);
    void AddToScene(Sleak::SceneBase* scene);
    void RemoveFromScene(Sleak::SceneBase* scene);

    int GetChunkX() const { return m_cx; }
    int GetChunkY() const { return m_cy; }
    int GetChunkZ() const { return m_cz; }

    bool IsMeshBuilt() const { return m_meshBuilt; }
    bool HasPendingMesh() const { return m_hasPendingMesh; }
    void ClearPendingMesh() { m_hasPendingMesh = false; }
    bool IsInFlight() const { return m_inFlight; }
    void SetInFlight(bool v) { m_inFlight = v; }
    Sleak::GameObject* GetGameObject() const { return m_gameObject; }

    bool IsDirty() const { return m_dirty; }
    void SetDirty(bool d) { m_dirty = d; }
    const uint8_t* GetBlockData() const { return m_blocks; }

    bool HasAllNeighbors() const {
        for (int i = 0; i < 6; ++i)
            if (!m_neighbors[i]) return false;
        return true;
    }
    int CountNeighbors() const {
        int count = 0;
        for (int i = 0; i < 6; ++i)
            if (m_neighbors[i]) ++count;
        return count;
    }
    bool NeedsMeshRebuild() const { return m_needsRebuild; }
    void SetNeedsMeshRebuild(bool v) { m_needsRebuild = v; }
    // Atomic: worker clears it after Generate() finishes writing blocks
    bool NeedsGeneration() const {
        return m_needsGeneration.load(std::memory_order_acquire);
    }
    void SetNeedsGeneration(bool v) {
        m_needsGeneration.store(v, std::memory_order_release);
    }

    int GetActiveIndex() const { return m_activeIndex; }
    void SetActiveIndex(int idx) { m_activeIndex = idx; }

    ChunkMeshData& GetPendingMeshData() { return m_pendingMesh; }
    ChunkMeshData& GetPendingWaterMeshData() { return m_pendingWaterMesh; }
    bool HasPendingWaterMesh() const { return m_hasPendingWaterMesh; }
    void ClearPendingWaterMesh() { m_hasPendingWaterMesh = false; }

    // All contiguous runs (>= 4 layers) of fully-opaque horizontal
    // layers per 8x8 XZ quadrant (q = (x>=8) + 2*(z>=8)), ascending,
    // published with the mesh data. At most 3 such runs fit in 16
    // layers. Returns run count, fills outMinY/outMaxY.
    static constexpr int OCC_MAX_RUNS = 3;
    int GetOccluderRuns(int q, int8_t outMinY[OCC_MAX_RUNS],
                        int8_t outMaxY[OCC_MAX_RUNS]) const {
        int n = 0;
        for (int i = 0; i < OCC_MAX_RUNS && m_occRunMinY[q][i] >= 0; ++i) {
            outMinY[n] = m_occRunMinY[q][i];
            outMaxY[n] = m_occRunMaxY[q][i];
            ++n;
        }
        return n;
    }

private:
    static int BlockIndex(int x, int y, int z) {
        return x + z * SIZE + y * SIZE * SIZE;
    }

    bool IsBlockSolidAt(int x, int y, int z) const;
    bool IsBlockOpaqueAt(int x, int y, int z) const;

    uint8_t m_blocks[VOLUME];
    Chunk* m_neighbors[6] = {};
    int m_cx, m_cy, m_cz;
    Sleak::GameObject* m_gameObject = nullptr;
    bool m_meshBuilt = false;
    bool m_addedToScene = false;
    ChunkMeshData m_pendingMesh;
    ChunkMeshData m_pendingWaterMesh;
    bool m_hasPendingMesh = false;
    bool m_hasPendingWaterMesh = false;
    bool m_inFlight = false;
    bool m_dirty = false;
    bool m_needsRebuild = false;
    std::atomic<bool> m_needsGeneration{true};
    int m_activeIndex = -1;
    int8_t m_occRunMinY[4][OCC_MAX_RUNS] = {
        {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};
    int8_t m_occRunMaxY[4][OCC_MAX_RUNS] = {
        {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};
};

#endif
