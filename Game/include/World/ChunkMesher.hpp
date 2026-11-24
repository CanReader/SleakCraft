#ifndef _CHUNK_MESHER_HPP_
#define _CHUNK_MESHER_HPP_

#include <Math/AABB.hpp>
#include <Runtime/MeshBatch.hpp>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "World/Chunk.hpp"
#include "World/VoxelVertex.hpp"

class ChunkManager;

/// Chunks per band: one column mesh merges this many chunks along Y.
inline constexpr int BAND_SIZE = 8;
/// Chunk slots of a column buffer, one per chunk of its band.
inline constexpr int COLUMN_CHUNKS = BAND_SIZE;

/// Band chunk of a slot in a direction group: the face directions run
/// top-down (surface before caves), the always-kept group bottom-up (back to
/// front seen from above, the order its two-sided faces always had).
constexpr int SlotChunk(int group, int slot) {
    return group == VOXEL_GROUP_ALWAYS ? slot : COLUMN_CHUNKS - 1 - slot;
}

/// Quad offsets of one stream in its column buffer: the quads of group g in
/// slot k are [start[g * COLUMN_CHUNKS + k], start[g * COLUMN_CHUNKS + k + 1]).
struct StreamRanges {
    uint32_t start[VOXEL_DIR_GROUPS * COLUMN_CHUNKS + 1] = {};
};

/// Floor-division: negative cy must map downward (e.g. cy=-1 -> band -1, not 0)
inline int ChunkYToBand(int cy) {
    return (cy >= 0) ? cy / BAND_SIZE : (cy - BAND_SIZE + 1) / BAND_SIZE;
}

/// Identifies one merged column mesh: an XZ column within a single Y band.
struct ColumnKey {
    int x, yBand, z;
    bool operator==(const ColumnKey& o) const { return x == o.x && yBand == o.yBand && z == o.z; }
};

/// Hash for ColumnKey, so columns can live in unordered containers.
struct ColumnKeyHash {
    size_t operator()(const ColumnKey& c) const {
        size_t h = std::hash<int>()(c.x);
        h ^= std::hash<int>()(c.yBand) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/// GPU mesh, range tables and culling state of one column band.
struct ColumnMesh {
    Sleak::MeshHandle mesh;  ///< every stream, on the shared quad indices
    StreamRanges ranges[VOXEL_STREAM_COUNT];
    bool visible = true;
    bool castsShadow = true;  // false for distant columns (skip shadow pass)
    Sleak::Math::AABB bounds;
    std::vector<Sleak::Math::AABB> occluders;
    float distSq = 0.0f;
    uint32_t liveIndex = 0;  ///< slot in ChunkManager::GetLiveColumns
};

using ColumnMeshMap = std::unordered_map<ColumnKey, ColumnMesh, ColumnKeyHash>;
using ColumnKeySet = std::unordered_set<ColumnKey, ColumnKeyHash>;

/// Merges a band of chunk meshes into one column mesh and uploads it.
/// @ingroup world
class ChunkMesher {
public:
    /// Binds the mesher to the manager owning the chunk grid and column map.
    explicit ChunkMesher(ChunkManager& mgr) : m_mgr(mgr) {}

    /// Rebuilds the column mesh for one band, deferring to workers if allowed.
    void RebuildColumnMesh(int cx, int yBand, int cz, bool allowDefer = true);

private:
    ChunkManager& m_mgr;

    // Merge scratch, reused across rebuilds
    VoxelVertexBuffer m_mergedVerts;
};

#endif
