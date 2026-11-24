#ifndef _CHUNK_HPP_
#define _CHUNK_HPP_

#include "Block.hpp"
#include "VoxelVertex.hpp"
#include <atomic>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <Memory/RefPtr.hpp>

namespace Sleak {
    class Material;
    class GameObject;
    class SceneBase;
}

/// Streams of a column vertex buffer, in buffer order.
enum class VoxelStream : uint8_t { Opaque, Water, Count };
/// Streams a chunk publishes and a column buffer holds.
inline constexpr int VOXEL_STREAM_COUNT = static_cast<int>(VoxelStream::Count);
/// Direction groups of a stream: BlockFace order, then the faces direction
/// culling must keep.
inline constexpr int VOXEL_DIR_GROUPS = BLOCK_FACE_COUNT + 1;
/// Group of the faces never culled by direction: two-sided water, and
/// alpha-tested leaves, whose back faces OpenGL draws.
inline constexpr int VOXEL_GROUP_ALWAYS = BLOCK_FACE_COUNT;
/// Face-pair mask with every pair of faces connected: a chunk hiding nothing.
inline constexpr uint16_t FULL_FACE_MASK = 0x7FFF;

/// Quads produced by GenerateMeshData, staged for the column merge: one
/// vector in (stream, group) order and the quad count of each segment.
struct ChunkMeshData {
    VoxelVertexBuffer vertices;
    uint32_t quads[VOXEL_STREAM_COUNT][VOXEL_DIR_GROUPS] = {};
    float minY = 0.0f;  ///< lowest quad corner in blocks, if any quad
    float maxY = 0.0f;  ///< highest quad corner in blocks, if any quad
};

/// One 16x16x16 block of the voxel grid. Owns its block data and produces
/// mesh data on request; the GPU mesh and scene lifetime live on
/// ChunkManager/ChunkMesher, not here.
/// @ingroup world
class Chunk {
public:
    static constexpr int SIZE = 16;
    static constexpr int VOLUME = SIZE * SIZE * SIZE;
    /// One bit per (x, z) column of the chunk, at x + z * SIZE.
    using Columns = std::bitset<SIZE * SIZE>;

    /// Face pairs the chunk's open cells connect, and the covered-water
    /// inputs its column merge resolves. Water counts as open only under a
    /// water surface (the first non-water cell above it is air or leaves);
    /// leaves are always open.
    struct Visibility {
        /// Water like air.
        uint16_t seeThrough = FULL_FACE_MASK;
        /// Covered water closed, water reaching the top layer open.
        uint16_t coveredOpen = FULL_FACE_MASK;
        /// Covered water and water reaching the top layer closed.
        uint16_t coveredClosed = FULL_FACE_MASK;
        /// Some water lies under air or leaves inside the chunk.
        bool openWater = false;
        /// Columns with water in the top layer.
        Columns waterTop;
        /// Columns whose lowest non-water cell is air or leaves.
        Columns downOpen;
        /// Columns whose lowest non-water cell is opaque; in neither set,
        /// the column is all water.
        Columns downClosed;
    };

    /// Constructs an all-air chunk at the given chunk-space coordinate.
    Chunk(int cx, int cy, int cz);
    /// Frees the game object if it was never handed to a scene.
    ~Chunk();

    void SetBlock(int x, int y, int z, BlockType type);
    BlockType GetBlock(int x, int y, int z) const;

    /// Links this chunk to an adjacent chunk so face culling can see across
    /// the boundary; pass nullptr to unlink.
    void SetNeighbor(BlockFace face, Chunk* chunk);

    /// Face-culls the block data into m_pendingMesh with per-vertex ambient
    /// occlusion, and extracts the per-quadrant occluder runs. CPU-only;
    /// does not touch the GPU or scene.
    void GenerateMeshData();
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

    /// True once all six face neighbors are linked via SetNeighbor.
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
    /// Atomic: worker clears it after Generate() finishes writing blocks
    bool NeedsGeneration() const {
        return m_needsGeneration.load(std::memory_order_acquire);
    }
    void SetNeedsGeneration(bool v) {
        m_needsGeneration.store(v, std::memory_order_release);
    }

    int GetActiveIndex() const { return m_activeIndex; }
    void SetActiveIndex(int idx) { m_activeIndex = idx; }

    ChunkMeshData& GetPendingMeshData() { return m_pendingMesh; }
    /// Connectivity published with the mesh data.
    const Visibility& GetVisibility() const { return m_visibility; }

    /// All contiguous runs (>= 4 layers) of fully-opaque horizontal
    /// layers per 8x8 XZ quadrant (q = (x>=8) + 2*(z>=8)), ascending,
    /// published with the mesh data. At most 3 such runs fit in 16
    /// layers. Returns run count, fills outMinY/outMaxY.
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

    /// Solidity at a local coordinate, crossing into a linked neighbor when
    /// out of range; treats missing neighbors at the world ceiling/floor as air.
    bool IsBlockSolidAt(int x, int y, int z) const;
    /// Opacity at a local coordinate, with the same neighbor-crossing rules
    /// as IsBlockSolidAt, except that below the world floor reads opaque.
    bool IsBlockOpaqueAt(int x, int y, int z) const;

    uint8_t m_blocks[VOLUME];
    Chunk* m_neighbors[6] = {};
    int m_cx, m_cy, m_cz;
    Sleak::GameObject* m_gameObject = nullptr;
    bool m_meshBuilt = false;
    bool m_addedToScene = false;
    ChunkMeshData m_pendingMesh;
    Visibility m_visibility;
    bool m_hasPendingMesh = false;
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
