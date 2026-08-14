#ifndef _CHUNK_MANAGER_HPP_
#define _CHUNK_MANAGER_HPP_

#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#include "ChunkRenderer.hpp"
#include "HeightmapCache.hpp"
#include "VoxelQueries.hpp"
#include "WorldGenerator.hpp"
#include <Math/AABB.hpp>
#include <Math/Vector.hpp>
#include <Memory/RefPtr.hpp>
#include <Runtime/MeshBatch.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <climits>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <array>

namespace Sleak {
    class Material;
    class SceneBase;
}

struct ChunkCoord {
    int x, y, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h = 0;
        h ^= std::hash<int>()(c.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();

    void Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material);
    void Update(float playerX, float playerY, float playerZ);

    void FlushPendingChunks();
    void SetRenderDistance(int chunks);

    int GetRenderDistance() const { return m_renderDistance; }

    void SetMultithreaded(bool enabled);
    bool IsMultithreaded() const { return m_multithreaded; }

    void SetDrawDistance(float dist) { m_drawDistance = dist; m_drawDistSq = dist * dist; }
    float GetDrawDistance() const { return m_drawDistance; }

    void SetShadowCasterDistance(float dist) { m_shadowCasterDistSq = dist * dist; }

    void SetSeed(uint32_t seed) { m_generator.SetSeed(seed); }
    uint32_t GetSeed() const { return m_generator.GetSeed(); }
    const WorldGenerator& GetGenerator() const { return m_generator; }

    // Render all visible column meshes via MeshBatch (call from scene Update)
    void RenderColumns();
    void RenderWater();

    // Forwards to CullingSystem frustum/occlusion toggles (used by UI).
    void SetCullingEnabled(bool frustum, bool occlusion);

    void SetWaterMaterial(const Sleak::RefPtr<Sleak::Material>& material) { m_waterMaterial = material; }

    BlockType GetBlockAt(int worldX, int worldY, int worldZ) const;
    bool SetBlockAt(int worldX, int worldY, int worldZ, BlockType type);
    void FlushPendingEdits();
    VoxelRaycastResult VoxelRaycast(const Sleak::Math::Vector3D& origin,
                                     const Sleak::Math::Vector3D& direction,
                                     float maxDist) const;
    VoxelCollisionResult ResolveVoxelCollision(const Sleak::Math::Vector3D& eyePos,
                                                float halfWidth, float height,
                                                float eyeOffset) const;

    // Save/load support
    struct DirtyChunkInfo {
        int cx, cy, cz;
        const uint8_t* blockData;
    };
    std::vector<DirtyChunkInfo> GetDirtyChunks() const;
    void ClearDirtyFlags();
    void LoadChunkData(const std::unordered_map<int64_t, std::array<uint8_t, 4096>>& data);
    void ForceReload();

    // Heightmap cache — persists m_columnMaxCyCache across sessions so
    // GetMaxFilledChunkY() noise evaluations are skipped on reload.
    void SaveHeightmapCache(const std::string& path) const;
    void LoadHeightmapCache(const std::string& path);

    // Collaborator access — used by ChunkMesher and ChunkRenderer.
    /// Loaded chunk at a chunk coordinate, or nullptr.
    Chunk* GetChunk(int cx, int cy, int cz);
    /// True while any of the six neighbors is owned by a worker thread.
    bool IsNeighborOfInFlight(const ChunkCoord& coord) const;
    /// Column meshes keyed by band: written by the mesher, read by the renderer.
    ColumnMeshMap& GetColumns() { return m_columns; }
    /// Bands awaiting a rebuild; the mesher re-inserts deferred keys here.
    ColumnKeySet& GetDirtyColumns() { return m_dirtyColumns; }
    /// Worker task queue and its guards, for batch mesh dispatch.
    std::mutex& GetTaskMutex() { return m_taskMutex; }
    std::vector<Chunk*>& GetTaskQueue() { return m_taskQueue; }
    std::condition_variable& GetTaskCV() { return m_taskCV; }
    /// Records a failed GPU mesh allocation so Update re-queues the column.
    void SetOOMThisFrame(bool oom) { m_oomThisFrame = oom; }
    float GetDrawDistSq() const { return m_drawDistSq; }
    float GetShadowCasterDistSq() const { return m_shadowCasterDistSq; }
    const Sleak::RefPtr<Sleak::Material>& GetMaterial() const { return m_material; }
    const Sleak::RefPtr<Sleak::Material>& GetWaterMaterial() const { return m_waterMaterial; }

private:
    void LinkNeighbors(const ChunkCoord& coord, Chunk* chunk);
    void UnlinkNeighbors(const ChunkCoord& coord, Chunk* chunk);
    const Chunk* GetChunk(int cx, int cy, int cz) const;

    void StartWorkers();
    void StopWorkers();
    void WorkerThread();

    // Column mesh management — merges all Y chunks per XZ column into one mesh
    void RebuildColumnMesh(int cx, int yBand, int cz, bool allowDefer = true);
    // Max number of column meshes before we consider VRAM exhausted.
    // At ~1.1 MB per column (96 bytes/vertex * ~12000 vertices), 800
    // columns ≈ 880 MB mesh VRAM — conservative for a 6 GB GPU with
    // other allocations (textures, framebuffers, etc.).
    static constexpr int MAX_COLUMN_MESHES = 800;

    ColumnMeshMap m_columns;
    ColumnKeySet m_dirtyColumns;
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_chunksNeedingRemesh;

    void UpdateVisibility();
    void BuildLoadSpiral();

    void ForceUnloadChunk(Chunk* chunk);

    std::vector<Chunk*> m_chunkGrid;
    int m_gridWidth = 1;
    int m_gridHeight = 8;
    int GetGridIndex(int cx, int cy, int cz) const;

    std::vector<Chunk*> m_activeChunks;
    std::vector<std::pair<int, int>> m_loadSpiral;

    std::vector<ChunkCoord> m_pendingLoad;
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_pendingSet;
    std::vector<ChunkCoord> m_pendingUnload;

    // Block edits deferred while a worker held the target/neighbor chunk
    struct PendingBlockEdit { int x, y, z; BlockType type; };
    std::vector<PendingBlockEdit> m_pendingEdits;

    // Edited chunks unloaded before a save — data lives in m_savedBlockData
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_stashedDirty;
    void StashIfDirty(Chunk* chunk);
    Sleak::SceneBase* m_scene = nullptr;
    Sleak::RefPtr<Sleak::Material> m_material;
    Sleak::RefPtr<Sleak::Material> m_waterMaterial;
    int m_renderDistance = 8;
    int m_chunksPerFrame = 32;    // remesh + unload budget per frame
    int m_dispatchPerFrame = 64;  // new-chunk generation dispatches per frame
    int m_uploadsPerFrame = 16;   // column mesh rebuilds (GPU uploads) per frame
    float m_drawDistance = 96.0f;
    float m_drawDistSq = 96.0f * 96.0f;
    float m_shadowCasterDistSq = 96.0f * 96.0f;
    int m_lastCenterX = INT_MAX;
    int m_lastCenterY = INT_MAX;
    int m_lastCenterZ = INT_MAX;
    bool m_oomThisFrame = false;
    WorldGenerator m_generator;

    // Multithreading
    bool m_multithreaded = false;
    std::vector<std::thread> m_workers;
    std::mutex m_taskMutex;
    std::condition_variable m_taskCV;
    std::vector<Chunk*> m_taskQueue;
    std::mutex m_readyMutex;
    std::vector<Chunk*> m_readyQueue;
    std::atomic<bool> m_shutdown{false};

    // Saved block data for chunk restoration
    std::unordered_map<int64_t, std::array<uint8_t, 4096>> m_savedBlockData;
    static int64_t PackCoord(int32_t cx, int32_t cy, int32_t cz);

    // Per-column max filled chunk-Y cache. Terrain is deterministic so entries
    // never go stale. Eliminates repeated noise evaluation for the same column.
    ColumnMaxCyMap m_columnMaxCyCache;
    int GetCachedColumnMaxCy(int cx, int cz);

    VoxelQueries m_queries{*this};
    ChunkMesher m_mesher{*this};
    ChunkRenderer m_renderer{*this};
};

#endif
