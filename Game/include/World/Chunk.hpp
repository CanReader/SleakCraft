#ifndef _CHUNK_HPP_
#define _CHUNK_HPP_

#include "Block.hpp"
#include <cstdint>
#include <cstring>
#include <Runtime/MeshData.hpp>
#include <Memory/RefPtr.h>

namespace Sleak {
    class Material;
    class GameObject;
    class SceneBase;
}

class Chunk {
public:
    static constexpr int SIZE = 16;
    static constexpr int VOLUME = SIZE * SIZE * SIZE;

    Chunk(int cx, int cy, int cz);
    ~Chunk();

    void SetBlock(int x, int y, int z, BlockType type);
    BlockType GetBlock(int x, int y, int z) const;

    void SetNeighbor(BlockFace face, Chunk* chunk);

    void BuildMesh(const Sleak::RefPtr<Sleak::Material>& material);
    void AddToScene(Sleak::SceneBase* scene);
    void RemoveFromScene(Sleak::SceneBase* scene);

    int GetChunkX() const { return m_cx; }
    int GetChunkY() const { return m_cy; }
    int GetChunkZ() const { return m_cz; }

    bool IsMeshBuilt() const { return m_meshBuilt; }
    Sleak::GameObject* GetGameObject() const { return m_gameObject; }

private:
    static int BlockIndex(int x, int y, int z) {
        return x + z * SIZE + y * SIZE * SIZE;
    }

    bool IsBlockSolidAt(int x, int y, int z) const;
    void AddFace(BlockFace face, int x, int y, int z, BlockType type,
                 Sleak::VertexGroup& vertices, Sleak::IndexGroup& indices);

    uint8_t m_blocks[VOLUME];
    Chunk* m_neighbors[6] = {};
    int m_cx, m_cy, m_cz;
    Sleak::GameObject* m_gameObject = nullptr;
    bool m_meshBuilt = false;
    bool m_addedToScene = false;
};

#endif
