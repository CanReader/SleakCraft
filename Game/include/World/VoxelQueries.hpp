#ifndef _VOXEL_QUERIES_HPP_
#define _VOXEL_QUERIES_HPP_

#include "Block.hpp"
#include <Math/Vector.hpp>

class ChunkManager;

/// Positional delta to resolve an AABB out of the blocks it overlapped.
struct VoxelCollisionResult {
    Sleak::Math::Vector3D correction{0.0f, 0.0f, 0.0f};
    bool onGround = false;
    bool hitCeiling = false;
    bool hitWall = false;
};

/// Result of a voxel raycast: the hit block and the empty cell just before it.
struct VoxelRaycastResult {
    bool hit = false;
    int blockX = 0, blockY = 0, blockZ = 0;
    int placeX = 0, placeY = 0, placeZ = 0;
    BlockType blockType = BlockType::Air;
};

/// Voxel ray and AABB collision queries against a ChunkManager block field.
class VoxelQueries {
public:
    explicit VoxelQueries(ChunkManager& mgr) : m_mgr(mgr) {}

    /// 3D-DDA raycast; steps cell-by-cell along the ray up to maxDist and
    /// stops at the first solid block, also reporting the adjacent cell to
    /// place a block into.
    VoxelRaycastResult VoxelRaycast(const Sleak::Math::Vector3D& origin,
                                     const Sleak::Math::Vector3D& direction,
                                     float maxDist) const;
    /// Iteratively pushes a player-sized AABB out of every solid block it
    /// overlaps, one axis at a time via per-block minimum translation
    /// vector (MTV), single-pass rather than split Y-first or XZ-first, to
    /// avoid the teleport-on-top and ground-shake bugs those splits cause.
    VoxelCollisionResult ResolveVoxelCollision(const Sleak::Math::Vector3D& eyePos,
                                                float halfWidth, float height,
                                                float eyeOffset) const;

private:
    ChunkManager& m_mgr;
};

#endif
