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

/// Chunk-grid coordinate (chunk units, not blocks).
struct ChunkCoord {
    int x, y, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && y == o.y && z == o.z; }
};

/// Hash for ChunkCoord, so coordinates can live in unordered containers.
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h = 0;
        h ^= std::hash<int>()(c.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/// Owns the chunk grid and drives streaming (spiral load/unload around the
/// player), block edits, and save/load hooks. Raycasts and collision are
/// delegated to VoxelQueries, meshing to ChunkMesher, and rendering/culling
/// to ChunkRenderer; those collaborators reach back in via the accessors
/// below.
class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();

    /// Binds the owning scene/material and sizes the load spiral for the
    /// initial render distance.
    void Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material);
    /// Retries deferred edits, streams chunks in/out around the player, and
    /// dispatches mesh rebuilds within this frame's budgets.
    void Update(float playerX, float playerY, float playerZ);

    /// Synchronously generates, links, and meshes every still-pending chunk;
    /// used for the initial spawn area so the player has ground immediately.
    void FlushPendingChunks();
    /// Changes the streaming radius; shrinking frees now out-of-range chunks
    /// and column meshes immediately instead of via the gradual unloader.
    void SetRenderDistance(int chunks);

    int GetRenderDistance() const { return m_renderDistance; }

    /// Starts or stops the worker thread pool for background generation/meshing.
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
    /// Places a block, creating its chunk on demand; if the target or a
    /// neighbor is owned by a worker the edit is queued and retried next
    /// Update. Otherwise remeshes the chunk, any boundary neighbors, and
    /// their column meshes synchronously.
    bool SetBlockAt(int worldX, int worldY, int worldZ, BlockType type);
    /// Applies edits that were deferred while their chunk was in flight;
    /// used before save so no queued edit is lost.
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
    /// Block data for every dirty active chunk, plus dirty chunks that were
    /// stashed on unload; the save path serializes this list.
    std::vector<DirtyChunkInfo> GetDirtyChunks() const;
    void ClearDirtyFlags();
    /// Seeds the stashed-block-data cache from a loaded save so matching
    /// chunks restore instead of regenerating.
    void LoadChunkData(const std::unordered_map<int64_t, std::array<uint8_t, 4096>>& data);
    /// Drops every chunk and column mesh and resets streaming state, forcing
    /// a full regenerate/reload on the next Update; used by F6 reload.
    void ForceReload();

    // Heightmap cache — persists m_columnMaxCyCache across sessions so
    // GetMaxFilledChunkY() noise evaluations are skipped on reload.
    void SaveHeightmapCache(const std::string& path) const;
    void LoadHeightmapCache(const std::string& path);

    // Collaborator access, used by ChunkMesher and ChunkRenderer.
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
    /// Links a newly-loaded chunk to its existing grid neighbors on both
    /// sides and flags any now-adjacent neighbor for a mesh rebuild.
    void LinkNeighbors(const ChunkCoord& coord, Chunk* chunk);
    /// Clears the neighbor pointers a chunk and its neighbors held on each
    /// other, run before a chunk is unloaded.
    void UnlinkNeighbors(const ChunkCoord& coord, Chunk* chunk);
    const Chunk* GetChunk(int cx, int cy, int cz) const;

    /// Spins up the background generation/meshing thread pool.
    void StartWorkers();
    /// Signals shutdown, joins all workers, and requeues any in-flight or
    /// completed-but-undrained chunks so they aren't stuck marked in-flight.
    void StopWorkers();
    /// Worker loop: steals a batch of queued chunks, generates/meshes them,
    /// and publishes results to the ready queue for the main thread to drain.
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
    /// Rebuilds the XZ load order (nearest-first) and regrids the chunk
    /// storage array when the render distance grows past its current size.
    void BuildLoadSpiral();

    /// Removes a chunk from the grid/active list, stashing its block data
    /// first if dirty. Does not delete the chunk.
    void ForceUnloadChunk(Chunk* chunk);

    std::vector<Chunk*> m_chunkGrid;
    int m_gridWidth = 1;
    int m_gridHeight = 8;
    /// Flat index into m_chunkGrid for a chunk coordinate, or -1 if cy is
    /// outside the world's vertical bounds.
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
    /// Copies a chunk's block data into m_savedBlockData if it's dirty, so
    /// the edit survives past the chunk's unload.
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
    /// Packs a chunk coordinate into one key for m_savedBlockData; must stay
    /// identical to SaveManager::PackCoord.
    static int64_t PackCoord(int32_t cx, int32_t cy, int32_t cz);

    // Per-column max filled chunk-Y cache. Terrain is deterministic so entries
    // never go stale. Eliminates repeated noise evaluation for the same column.
    ColumnMaxCyMap m_columnMaxCyCache;
    /// Cached (or freshly-evaluated and cached) highest generated chunk-Y
    /// for an XZ column.
    int GetCachedColumnMaxCy(int cx, int cz);

    VoxelQueries m_queries{*this};
    ChunkMesher m_mesher{*this};
    ChunkRenderer m_renderer{*this};
};

#endif
