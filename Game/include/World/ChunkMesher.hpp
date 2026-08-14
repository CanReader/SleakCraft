#ifndef _CHUNK_MESHER_HPP_
#define _CHUNK_MESHER_HPP_

#include <Math/AABB.hpp>
#include <Runtime/MeshBatch.hpp>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ChunkManager;

/// Chunks per band: one column mesh merges this many chunks along Y.
inline constexpr int BAND_SIZE = 8;

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

/// GPU meshes plus culling state for one column band.
struct ColumnMesh {
    Sleak::MeshHandle mesh;
    Sleak::MeshHandle waterMesh;
    bool visible = true;
    bool castsShadow = true;  // false for distant columns (skip shadow pass)
    Sleak::Math::AABB bounds;
    std::vector<Sleak::Math::AABB> occluders;
    float distSq = 0.0f;
};

using ColumnMeshMap = std::unordered_map<ColumnKey, ColumnMesh, ColumnKeyHash>;
using ColumnKeySet = std::unordered_set<ColumnKey, ColumnKeyHash>;

/// Merges a band of chunk meshes into one column mesh and uploads it.
class ChunkMesher {
public:
    /// Binds the mesher to the manager owning the chunk grid and column map.
    explicit ChunkMesher(ChunkManager& mgr) : m_mgr(mgr) {}

    /// Rebuilds the column mesh for one band, deferring to workers if allowed.
    void RebuildColumnMesh(int cx, int yBand, int cz, bool allowDefer = true);

private:
    ChunkManager& m_mgr;
};

#endif
