#include "World/Chunk.hpp"
#include "World/TextureAtlas.hpp"
#include <Core/GameObject.hpp>
#include <Core/SceneBase.hpp>
#include <ECS/Components/TransformComponent.hpp>
#include <ECS/Components/MeshComponent.hpp>
#include <ECS/Components/MaterialComponent.hpp>
#include <Math/Vector.hpp>
#include <Runtime/Material.hpp>

using namespace Sleak;
using namespace Sleak::Math;

Chunk::Chunk(int cx, int cy, int cz) : m_cx(cx), m_cy(cy), m_cz(cz) {
    memset(m_blocks, static_cast<uint8_t>(BlockType::Air), VOLUME);
}

Chunk::~Chunk() {
    if (m_gameObject && !m_addedToScene)
        delete m_gameObject;
}

void Chunk::SetBlock(int x, int y, int z, BlockType type) {
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE) return;
    m_blocks[BlockIndex(x, y, z)] = static_cast<uint8_t>(type);
}

BlockType Chunk::GetBlock(int x, int y, int z) const {
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE)
        return BlockType::Air;
    return static_cast<BlockType>(m_blocks[BlockIndex(x, y, z)]);
}

void Chunk::SetNeighbor(BlockFace face, Chunk* chunk) {
    m_neighbors[static_cast<uint8_t>(face)] = chunk;
}

bool Chunk::IsBlockSolidAt(int x, int y, int z) const {
    if (x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE)
        return IsBlockSolid(static_cast<BlockType>(m_blocks[BlockIndex(x, y, z)]));

    if (y >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Top)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y - SIZE, z));
        // No chunk above — at world ceiling, treat as air so top faces render
        return false;
    }
    if (y < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Bottom)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y + SIZE, z));
        return false;
    }
    if (z >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::North)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y, z - SIZE));
    }
    if (z < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::South)];
        if (nb) return IsBlockSolid(nb->GetBlock(x, y, z + SIZE));
    }
    if (x >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::East)];
        if (nb) return IsBlockSolid(nb->GetBlock(x - SIZE, y, z));
    }
    if (x < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::West)];
        if (nb) return IsBlockSolid(nb->GetBlock(x + SIZE, y, z));
    }

    return true;
}

bool Chunk::IsBlockOpaqueAt(int x, int y, int z) const {
    if (x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE)
        return IsBlockOpaque(static_cast<BlockType>(m_blocks[BlockIndex(x, y, z)]));

    if (y >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Top)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y - SIZE, z));
        return false;
    }
    if (y < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::Bottom)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y + SIZE, z));
        return false;
    }
    if (z >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::North)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y, z - SIZE));
    }
    if (z < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::South)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x, y, z + SIZE));
    }
    if (x >= SIZE) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::East)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x - SIZE, y, z));
    }
    if (x < 0) {
        auto* nb = m_neighbors[static_cast<uint8_t>(BlockFace::West)];
        if (nb) return IsBlockOpaque(nb->GetBlock(x + SIZE, y, z));
    }

    return true;
}

void Chunk::GenerateMeshData() {
    VertexGroup vertices;
    IndexGroup indices;

    bool opaque[18][18][18];
    bool solid[18][18][18];
    for (int y = -1; y <= SIZE; ++y) {
        for (int z = -1; z <= SIZE; ++z) {
            for (int x = -1; x <= SIZE; ++x) {
                opaque[y+1][z+1][x+1] = IsBlockOpaqueAt(x, y, z);
                solid[y+1][z+1][x+1]  = IsBlockSolidAt(x, y, z);
            }
        }
    }

    auto fastCalcAO = [](bool side1, bool side2, bool corner) {
        if (side1 && side2) return 0;
        return 3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner));
    };

    auto fastFaceAO = [&](BlockFace face, int x, int y, int z, float ao[4]) {
        static constexpr float AO_TABLE[] = {0.25f, 0.55f, 0.8f, 1.0f};

        auto isSolid = [&](int dx, int dy, int dz) {
            return solid[dy+1][dz+1][dx+1];
        };

        switch (face) {
            case BlockFace::Top: {
                int ay = y + 1;
                ao[0] = AO_TABLE[fastCalcAO(isSolid(x-1,ay,z),   isSolid(x,ay,z-1),   isSolid(x-1,ay,z-1))];
                ao[1] = AO_TABLE[fastCalcAO(isSolid(x-1,ay,z),   isSolid(x,ay,z+1),   isSolid(x-1,ay,z+1))];
                ao[2] = AO_TABLE[fastCalcAO(isSolid(x+1,ay,z),   isSolid(x,ay,z+1),   isSolid(x+1,ay,z+1))];
                ao[3] = AO_TABLE[fastCalcAO(isSolid(x+1,ay,z),   isSolid(x,ay,z-1),   isSolid(x+1,ay,z-1))];
                break;
            }
            case BlockFace::Bottom: {
                int ay = y - 1;
                ao[0] = AO_TABLE[fastCalcAO(isSolid(x-1,ay,z),   isSolid(x,ay,z+1),   isSolid(x-1,ay,z+1))];
                ao[1] = AO_TABLE[fastCalcAO(isSolid(x-1,ay,z),   isSolid(x,ay,z-1),   isSolid(x-1,ay,z-1))];
                ao[2] = AO_TABLE[fastCalcAO(isSolid(x+1,ay,z),   isSolid(x,ay,z-1),   isSolid(x+1,ay,z-1))];
                ao[3] = AO_TABLE[fastCalcAO(isSolid(x+1,ay,z),   isSolid(x,ay,z+1),   isSolid(x+1,ay,z+1))];
                break;
            }
            case BlockFace::North: {
                int az = z + 1;
                ao[0] = AO_TABLE[fastCalcAO(isSolid(x+1,y,az),   isSolid(x,y-1,az),   isSolid(x+1,y-1,az))];
                ao[1] = AO_TABLE[fastCalcAO(isSolid(x+1,y,az),   isSolid(x,y+1,az),   isSolid(x+1,y+1,az))];
                ao[2] = AO_TABLE[fastCalcAO(isSolid(x-1,y,az),   isSolid(x,y+1,az),   isSolid(x-1,y+1,az))];
                ao[3] = AO_TABLE[fastCalcAO(isSolid(x-1,y,az),   isSolid(x,y-1,az),   isSolid(x-1,y-1,az))];
                break;
            }
            case BlockFace::South: {
                int az = z - 1;
                ao[0] = AO_TABLE[fastCalcAO(isSolid(x-1,y,az),   isSolid(x,y-1,az),   isSolid(x-1,y-1,az))];
                ao[1] = AO_TABLE[fastCalcAO(isSolid(x-1,y,az),   isSolid(x,y+1,az),   isSolid(x-1,y+1,az))];
                ao[2] = AO_TABLE[fastCalcAO(isSolid(x+1,y,az),   isSolid(x,y+1,az),   isSolid(x+1,y+1,az))];
                ao[3] = AO_TABLE[fastCalcAO(isSolid(x+1,y,az),   isSolid(x,y-1,az),   isSolid(x+1,y-1,az))];
                break;
            }
            case BlockFace::East: {
                int ax = x + 1;
                ao[0] = AO_TABLE[fastCalcAO(isSolid(ax,y,z-1),   isSolid(ax,y-1,z),   isSolid(ax,y-1,z-1))];
                ao[1] = AO_TABLE[fastCalcAO(isSolid(ax,y,z-1),   isSolid(ax,y+1,z),   isSolid(ax,y+1,z-1))];
                ao[2] = AO_TABLE[fastCalcAO(isSolid(ax,y,z+1),   isSolid(ax,y+1,z),   isSolid(ax,y+1,z+1))];
                ao[3] = AO_TABLE[fastCalcAO(isSolid(ax,y,z+1),   isSolid(ax,y-1,z),   isSolid(ax,y-1,z+1))];
                break;
            }
            case BlockFace::West: {
                int ax = x - 1;
                ao[0] = AO_TABLE[fastCalcAO(isSolid(ax,y,z+1),   isSolid(ax,y-1,z),   isSolid(ax,y-1,z+1))];
                ao[1] = AO_TABLE[fastCalcAO(isSolid(ax,y,z+1),   isSolid(ax,y+1,z),   isSolid(ax,y+1,z+1))];
                ao[2] = AO_TABLE[fastCalcAO(isSolid(ax,y,z-1),   isSolid(ax,y+1,z),   isSolid(ax,y+1,z-1))];
                ao[3] = AO_TABLE[fastCalcAO(isSolid(ax,y,z-1),   isSolid(ax,y-1,z),   isSolid(ax,y-1,z-1))];
                break;
            }
        }
    };

    auto fastAddFace = [&](BlockFace face, int x, int y, int z, BlockType type) {
        AtlasUV uv = TextureAtlas::GetTileUV(GetBlockTextureTile(type, face));
        uint32_t base = static_cast<uint32_t>(vertices.GetSize());
        float bx = static_cast<float>(x + m_cx * SIZE);
        float by = static_cast<float>(y + m_cy * SIZE);
        float bz = static_cast<float>(z + m_cz * SIZE);

        float ao[4];
        fastFaceAO(face, x, y, z, ao);

        Vertex v[4];
        switch (face) {
            case BlockFace::Top:
                v[0] = Vertex(bx,     by + 1, bz,     0, 1, 0, 1, 0, 0, 1, uv.u0, uv.v1);
                v[1] = Vertex(bx,     by + 1, bz + 1, 0, 1, 0, 1, 0, 0, 1, uv.u0, uv.v0);
                v[2] = Vertex(bx + 1, by + 1, bz + 1, 0, 1, 0, 1, 0, 0, 1, uv.u1, uv.v0);
                v[3] = Vertex(bx + 1, by + 1, bz,     0, 1, 0, 1, 0, 0, 1, uv.u1, uv.v1);
                break;
            case BlockFace::Bottom:
                v[0] = Vertex(bx,     by, bz + 1, 0, -1, 0, 1, 0, 0, 1, uv.u0, uv.v1);
                v[1] = Vertex(bx,     by, bz,     0, -1, 0, 1, 0, 0, 1, uv.u0, uv.v0);
                v[2] = Vertex(bx + 1, by, bz,     0, -1, 0, 1, 0, 0, 1, uv.u1, uv.v0);
                v[3] = Vertex(bx + 1, by, bz + 1, 0, -1, 0, 1, 0, 0, 1, uv.u1, uv.v1);
                break;
            case BlockFace::North:
                v[0] = Vertex(bx + 1, by,     bz + 1, 0, 0, 1, 1, 0, 0, 1, uv.u0, uv.v1);
                v[1] = Vertex(bx + 1, by + 1, bz + 1, 0, 0, 1, 1, 0, 0, 1, uv.u0, uv.v0);
                v[2] = Vertex(bx,     by + 1, bz + 1, 0, 0, 1, 1, 0, 0, 1, uv.u1, uv.v0);
                v[3] = Vertex(bx,     by,     bz + 1, 0, 0, 1, 1, 0, 0, 1, uv.u1, uv.v1);
                break;
            case BlockFace::South:
                v[0] = Vertex(bx,     by,     bz, 0, 0, -1, 1, 0, 0, 1, uv.u0, uv.v1);
                v[1] = Vertex(bx,     by + 1, bz, 0, 0, -1, 1, 0, 0, 1, uv.u0, uv.v0);
                v[2] = Vertex(bx + 1, by + 1, bz, 0, 0, -1, 1, 0, 0, 1, uv.u1, uv.v0);
                v[3] = Vertex(bx + 1, by,     bz, 0, 0, -1, 1, 0, 0, 1, uv.u1, uv.v1);
                break;
            case BlockFace::East:
                v[0] = Vertex(bx + 1, by,     bz,     1, 0, 0, 0, 0, 1, 1, uv.u0, uv.v1);
                v[1] = Vertex(bx + 1, by + 1, bz,     1, 0, 0, 0, 0, 1, 1, uv.u0, uv.v0);
                v[2] = Vertex(bx + 1, by + 1, bz + 1, 1, 0, 0, 0, 0, 1, 1, uv.u1, uv.v0);
                v[3] = Vertex(bx + 1, by,     bz + 1, 1, 0, 0, 0, 0, 1, 1, uv.u1, uv.v1);
                break;
            case BlockFace::West:
                v[0] = Vertex(bx, by,     bz + 1, -1, 0, 0, 0, 0, -1, 1, uv.u0, uv.v1);
                v[1] = Vertex(bx, by + 1, bz + 1, -1, 0, 0, 0, 0, -1, 1, uv.u0, uv.v0);
                v[2] = Vertex(bx, by + 1, bz,     -1, 0, 0, 0, 0, -1, 1, uv.u1, uv.v0);
                v[3] = Vertex(bx, by,     bz,     -1, 0, 0, 0, 0, -1, 1, uv.u1, uv.v1);
                break;
        }

        for (int i = 0; i < 4; ++i) {
            v[i].SetColor(ao[i], ao[i], ao[i], 1.0f);
            vertices.AddVertex(v[i]);
        }

        if (ao[0] + ao[2] > ao[1] + ao[3]) {
            indices.add(base); indices.add(base + 2); indices.add(base + 1);
            indices.add(base); indices.add(base + 3); indices.add(base + 2);
        } else {
            indices.add(base); indices.add(base + 3); indices.add(base + 1);
            indices.add(base + 1); indices.add(base + 3); indices.add(base + 2);
        }
    };

    for (int y = 0; y < SIZE; ++y) {
        for (int z = 0; z < SIZE; ++z) {
            for (int x = 0; x < SIZE; ++x) {
                BlockType type = GetBlock(x, y, z);
                if (!IsBlockRenderable(type)) continue;

                if (!opaque[y + 1 + 1][z + 1][x + 1]) fastAddFace(BlockFace::Top,    x, y, z, type);
                if (!opaque[y - 1 + 1][z + 1][x + 1]) fastAddFace(BlockFace::Bottom, x, y, z, type);
                if (!opaque[y + 1][z + 1 + 1][x + 1]) fastAddFace(BlockFace::North,  x, y, z, type);
                if (!opaque[y + 1][z - 1 + 1][x + 1]) fastAddFace(BlockFace::South,  x, y, z, type);
                if (!opaque[y + 1][z + 1][x + 1 + 1]) fastAddFace(BlockFace::East,   x, y, z, type);
                if (!opaque[y + 1][z + 1][x - 1 + 1]) fastAddFace(BlockFace::West,   x, y, z, type);
            }
        }
    }

    m_pendingMesh.vertices = std::move(vertices);
    m_pendingMesh.indices = std::move(indices);
    m_hasPendingMesh = true;
    m_meshBuilt = true;
}

void Chunk::UploadMesh(const RefPtr<Material>& material) {
    if (!m_hasPendingMesh) return;
    m_hasPendingMesh = false;

    if (m_pendingMesh.vertices.GetSize() == 0) {
        m_meshBuilt = true;
        return;
    }

    delete m_gameObject;

    MeshData meshData;
    meshData.vertices = std::move(m_pendingMesh.vertices);
    meshData.indices = std::move(m_pendingMesh.indices);

    m_gameObject = new GameObject("Chunk");
    // Vertices are already in world-space, so transform is at origin
    m_gameObject->AddComponent<TransformComponent>(Vector3D(0.0f, 0.0f, 0.0f));
    m_gameObject->AddComponent<MaterialComponent>(material);
    m_gameObject->AddComponent<MeshComponent>(std::move(meshData));
    m_gameObject->Initialize();

    m_meshBuilt = true;
}

void Chunk::BuildMesh(const RefPtr<Material>& material) {
    GenerateMeshData();
    UploadMesh(material);
}

void Chunk::AddToScene(SceneBase* scene) {
    if (m_gameObject) {
        scene->AddObject(m_gameObject);
        m_addedToScene = true;
    }
}

void Chunk::RemoveFromScene(SceneBase* scene) {
    if (m_gameObject && m_addedToScene) {
        scene->RemoveObject(m_gameObject);
        m_gameObject = nullptr;
        m_addedToScene = false;
    }
}
