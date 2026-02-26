#ifndef _CHUNK_MANAGER_HPP_
#define _CHUNK_MANAGER_HPP_

#include "Chunk.hpp"
#include <Math/Vector.hpp>
#include <Memory/RefPtr.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <climits>

namespace Sleak {
    class Material;
    class SceneBase;
}

struct ChunkCoord {
    int x, y, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && y == o.y && z == o.z; }
};

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
    void Update(float playerX, float playerZ);

    void FlushPendingChunks();
    void SetRenderDistance(int chunks) { m_renderDistance = chunks; }
    int GetRenderDistance() const { return m_renderDistance; }

    void SetDrawDistance(float dist) { m_drawDistance = dist; m_drawDistSq = dist * dist; }
    float GetDrawDistance() const { return m_drawDistance; }

    void FrustumCull() const;

    BlockType GetBlockAt(int worldX, int worldY, int worldZ) const;
    bool SetBlockAt(int worldX, int worldY, int worldZ, BlockType type);
    VoxelRaycastResult VoxelRaycast(const Sleak::Math::Vector3D& origin,
                                     const Sleak::Math::Vector3D& direction,
                                     float maxDist) const;
    VoxelCollisionResult ResolveVoxelCollision(const Sleak::Math::Vector3D& eyePos,
                                                float halfWidth, float height,
                                                float eyeOffset) const;

private:
    void GenerateFlatTerrain(Chunk* chunk);
    void LinkNeighbors(const ChunkCoord& coord, Chunk* chunk);
    Chunk* GetChunk(int cx, int cy, int cz);
    const Chunk* GetChunk(int cx, int cy, int cz) const;

    std::unordered_map<ChunkCoord, Chunk*, ChunkCoordHash> m_chunks;
    std::vector<ChunkCoord> m_pendingLoad;
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_pendingSet;
    Sleak::SceneBase* m_scene = nullptr;
    Sleak::RefPtr<Sleak::Material> m_material;
    int m_renderDistance = 8;
    int m_chunksPerFrame = 4;
    float m_drawDistance = 96.0f;
    float m_drawDistSq = 96.0f * 96.0f;
    int m_lastCenterX = INT_MAX;
    int m_lastCenterZ = INT_MAX;

    static constexpr int SURFACE_HEIGHT = 4;
};

#endif
