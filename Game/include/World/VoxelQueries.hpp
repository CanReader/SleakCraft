#ifndef _VOXEL_QUERIES_HPP_
#define _VOXEL_QUERIES_HPP_

#include "Block.hpp"
#include <Math/Vector.hpp>

class ChunkManager;

struct VoxelCollisionResult {
    Sleak::Math::Vector3D correction{0.0f, 0.0f, 0.0f};
    bool onGround = false;
    bool hitCeiling = false;
    bool hitWall = false;
};

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

    VoxelRaycastResult VoxelRaycast(const Sleak::Math::Vector3D& origin,
                                     const Sleak::Math::Vector3D& direction,
                                     float maxDist) const;
    VoxelCollisionResult ResolveVoxelCollision(const Sleak::Math::Vector3D& eyePos,
                                                float halfWidth, float height,
                                                float eyeOffset) const;

private:
    ChunkManager& m_mgr;
};

#endif
