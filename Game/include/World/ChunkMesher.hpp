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
/// Every chunk of a column, bit per band chunk.
inline constexpr uint8_t ALL_COLUMN_CHUNKS = (1u << COLUMN_CHUNKS) - 1;

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

/// Appends the draw ranges of the (group, slot) segments keep accepts, in
/// buffer order; touching segments merge and empty ones never split a run.
template <typename Keep>
void AppendStreamRanges(const StreamRanges& r, Keep&& keep,
                        std::vector<Sleak::MeshRange>& out) {
    bool open = false;
    uint32_t first = 0;
    uint32_t end = 0;
    auto flush = [&] {
        out.push_back({0, (end - first) * VOXEL_QUAD_INDICES,
                       static_cast<int32_t>(first * VOXEL_QUAD_CORNERS)});
    };
    for (int i = 0; i < VOXEL_DIR_GROUPS * COLUMN_CHUNKS; ++i) {
        if (r.start[i] == r.start[i + 1]) continue;
        if (keep(i / COLUMN_CHUNKS, i % COLUMN_CHUNKS)) {
            if (!open) first = r.start[i];
            open = true;
            end = r.start[i + 1];
        } else if (open) {
            flush();
            open = false;
        }
    }
    if (open) flush();
}

/// How far the camera must be behind the planes of a direction's faces
/// before the direction is dropped, in blocks.
inline constexpr float DIR_CULL_EPSILON = 1.0f / 64.0f;

/// Direction groups of the chunk at (x0, y0, z0) that may face a camera at
/// (cx, cy, cz): a direction drops when the camera is behind all its faces.
constexpr uint8_t FacingGroups(float cx, float cy, float cz, float x0, float y0,
                               float z0) {
    // Positive faces lie 1..16 blocks into the chunk, negative ones 0..15
    constexpr float nearest = 1.0f - DIR_CULL_EPSILON;
    constexpr float farthest = Chunk::SIZE - 1 + DIR_CULL_EPSILON;
    auto bit = [](BlockFace face) {
        return static_cast<uint8_t>(1u << static_cast<int>(face));
    };
    uint8_t groups = 1u << VOXEL_GROUP_ALWAYS;
    if (cy > y0 + nearest) groups |= bit(BlockFace::Top);
    if (cy < y0 + farthest) groups |= bit(BlockFace::Bottom);
    if (cz > z0 + nearest) groups |= bit(BlockFace::North);
    if (cz < z0 + farthest) groups |= bit(BlockFace::South);
    if (cx > x0 + nearest) groups |= bit(BlockFace::East);
    if (cx < x0 + farthest) groups |= bit(BlockFace::West);
    return groups;
}

/// Culling data of one chunk of a column; unmeshed chunks read as air.
struct ChunkVis {
    /// Lowest quad corner in blocks.
    float minY = 0.0f;
    /// Highest quad corner in blocks.
    float maxY = 0.0f;
    /// Face pairs its open cells link, water like air.
    uint16_t seeThrough = FULL_FACE_MASK;
    /// Face pairs its open cells link, covered water closed.
    uint16_t covered = FULL_FACE_MASK;
    /// Drawn from the column buffer.
    bool hasQuads = false;
    /// Holds water under air.
    bool openWater = false;
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
    /// Its entry in ChunkManager::GetColumns.
    ColumnKey key{};
    Sleak::MeshHandle mesh;  ///< every stream, on the shared quad indices
    StreamRanges ranges[VOXEL_STREAM_COUNT];
    /// Per band chunk, bottom up.
    ChunkVis chunks[COLUMN_CHUNKS];
    /// The camera may see it this frame.
    bool visible = true;
    /// Casts shadows this frame.
    bool castsShadow = true;
    /// Band chunks the camera draws this frame, bit per chunk.
    uint8_t visibleChunks = 0;
    /// Band chunks the last visibility graph run reached; all of them
    /// until a run follows the rebuild.
    uint8_t reachableChunks = ALL_COLUMN_CHUNKS;
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
