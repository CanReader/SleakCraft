#include "World/VoxelQueries.hpp"
#include "World/ChunkManager.hpp"
#include <Runtime/Material.hpp>
#include <algorithm>
#include <cmath>

VoxelRaycastResult VoxelQueries::VoxelRaycast(
    const Sleak::Math::Vector3D& origin,
    const Sleak::Math::Vector3D& direction,
    float maxDist) const
{
    VoxelRaycastResult result;

    float ox = origin.GetX(), oy = origin.GetY(), oz = origin.GetZ();
    float dx = direction.GetX(), dy = direction.GetY(), dz = direction.GetZ();

    int x = static_cast<int>(std::floor(ox));
    int y = static_cast<int>(std::floor(oy));
    int z = static_cast<int>(std::floor(oz));

    int stepX = (dx >= 0) ? 1 : -1;
    int stepY = (dy >= 0) ? 1 : -1;
    int stepZ = (dz >= 0) ? 1 : -1;

    float tDeltaX = (dx != 0.0f) ? std::abs(1.0f / dx) : 1e30f;
    float tDeltaY = (dy != 0.0f) ? std::abs(1.0f / dy) : 1e30f;
    float tDeltaZ = (dz != 0.0f) ? std::abs(1.0f / dz) : 1e30f;

    float tMaxX = (dx != 0.0f) ? ((stepX > 0 ? (x + 1.0f - ox) : (ox - x)) * tDeltaX) : 1e30f;
    float tMaxY = (dy != 0.0f) ? ((stepY > 0 ? (y + 1.0f - oy) : (oy - y)) * tDeltaY) : 1e30f;
    float tMaxZ = (dz != 0.0f) ? ((stepZ > 0 ? (z + 1.0f - oz) : (oz - z)) * tDeltaZ) : 1e30f;

    int prevX = x, prevY = y, prevZ = z;
    float t = 0.0f;

    for (int i = 0; i < static_cast<int>(maxDist * 3.0f) + 1; ++i) {
        BlockType block = m_mgr.GetBlockAt(x, y, z);
        if (IsBlockSolid(block)) {
            result.hit = true;
            result.blockX = x;
            result.blockY = y;
            result.blockZ = z;
            result.placeX = prevX;
            result.placeY = prevY;
            result.placeZ = prevZ;
            result.blockType = block;
            return result;
        }

        prevX = x; prevY = y; prevZ = z;

        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                t = tMaxX;
                x += stepX;
                tMaxX += tDeltaX;
            } else {
                t = tMaxZ;
                z += stepZ;
                tMaxZ += tDeltaZ;
            }
        } else {
            if (tMaxY < tMaxZ) {
                t = tMaxY;
                y += stepY;
                tMaxY += tDeltaY;
            } else {
                t = tMaxZ;
                z += stepZ;
                tMaxZ += tDeltaZ;
            }
        }

        if (t > maxDist) break;
    }

    return result;
}

VoxelCollisionResult VoxelQueries::ResolveVoxelCollision(
    const Sleak::Math::Vector3D& eyePos,
    float halfWidth, float height, float eyeOffset) const
{
    using namespace Sleak::Math;
    VoxelCollisionResult result;

    float feetY = eyePos.GetY() - eyeOffset;
    float posX = eyePos.GetX();
    float posZ = eyePos.GetZ();

    auto computeAABB = [&](float fx, float fy, float fz,
                           float& minX, float& minY, float& minZ,
                           float& maxX, float& maxY, float& maxZ) {
        minX = fx - halfWidth;
        maxX = fx + halfWidth;
        minY = fy;
        maxY = fy + height;
        minZ = fz - halfWidth;
        maxZ = fz + halfWidth;
    };

    for (int iter = 0; iter < 16; ++iter) {
        float minX, minY, minZ, maxX, maxY, maxZ;
        computeAABB(posX, feetY, posZ, minX, minY, minZ, maxX, maxY, maxZ);

        int bx0 = static_cast<int>(std::floor(minX));
        int bx1 = static_cast<int>(std::floor(maxX - 0.0001f));
        int by0 = static_cast<int>(std::floor(minY));
        int by1 = static_cast<int>(std::floor(maxY - 0.0001f));
        int bz0 = static_cast<int>(std::floor(minZ));
        int bz1 = static_cast<int>(std::floor(maxZ - 0.0001f));

        bool corrected = false;
        for (int by = by0; by <= by1; ++by) {
            for (int bz = bz0; bz <= bz1; ++bz) {
                for (int bx = bx0; bx <= bx1; ++bx) {
                    if (!IsBlockSolid(m_mgr.GetBlockAt(bx, by, bz))) continue;

                    float blockMinX = static_cast<float>(bx);
                    float blockMaxX = static_cast<float>(bx + 1);
                    float blockMinY = static_cast<float>(by);
                    float blockMaxY = static_cast<float>(by + 1);
                    float blockMinZ = static_cast<float>(bz);
                    float blockMaxZ = static_cast<float>(bz + 1);

                    computeAABB(posX, feetY, posZ, minX, minY, minZ, maxX, maxY, maxZ);

                    if (minX >= blockMaxX || maxX <= blockMinX ||
                        minY >= blockMaxY || maxY <= blockMinY ||
                        minZ >= blockMaxZ || maxZ <= blockMinZ)
                        continue;

                    float pushXPos = blockMaxX - minX;
                    float pushXNeg = maxX - blockMinX;
                    float penX = std::min(pushXPos, pushXNeg);

                    float pushYPos = blockMaxY - minY;
                    float pushYNeg = maxY - blockMinY;
                    float penY = std::min(pushYPos, pushYNeg);

                    float pushZPos = blockMaxZ - minZ;
                    float pushZNeg = maxZ - blockMinZ;
                    float penZ = std::min(pushZPos, pushZNeg);

                    if (penY <= penX && penY <= penZ) {
                        if (pushYPos < pushYNeg) {
                            feetY += pushYPos;
                            result.onGround = true;
                        } else {
                            feetY -= pushYNeg;
                            result.hitCeiling = true;
                        }
                    } else if (penX <= penZ) {
                        if (pushXPos < pushXNeg)
                            posX += pushXPos;
                        else
                            posX -= pushXNeg;
                        result.hitWall = true;
                    } else {
                        if (pushZPos < pushZNeg)
                            posZ += pushZPos;
                        else
                            posZ -= pushZNeg;
                        result.hitWall = true;
                    }
                    corrected = true;
                }
            }
        }
        if (!corrected) break;
    }

    float newEyeY = feetY + eyeOffset;
    result.correction = Vector3D(posX - eyePos.GetX(),
                                  newEyeY - eyePos.GetY(),
                                  posZ - eyePos.GetZ());

    return result;
}
