#include "World/ChunkManager.hpp"
#include <Core/SceneBase.hpp>
#include <cmath>
#include <vector>

ChunkManager::ChunkManager() {}

ChunkManager::~ChunkManager() {
    for (auto& [coord, chunk] : m_chunks) {
        if (m_scene) chunk->RemoveFromScene(m_scene);
        delete chunk;
    }
    m_chunks.clear();
}

void ChunkManager::Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material) {
    m_scene = scene;
    m_material = material;
}

Chunk* ChunkManager::GetChunk(int cx, int cy, int cz) {
    ChunkCoord coord{cx, cy, cz};
    auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second : nullptr;
}

void ChunkManager::LinkNeighbors(const ChunkCoord& coord, Chunk* chunk) {
    struct { BlockFace face; int dx, dy, dz; BlockFace opposite; } dirs[] = {
        {BlockFace::Top,    0,  1,  0, BlockFace::Bottom},
        {BlockFace::Bottom, 0, -1,  0, BlockFace::Top},
        {BlockFace::North,  0,  0,  1, BlockFace::South},
        {BlockFace::South,  0,  0, -1, BlockFace::North},
        {BlockFace::East,   1,  0,  0, BlockFace::West},
        {BlockFace::West,  -1,  0,  0, BlockFace::East},
    };

    for (auto& d : dirs) {
        Chunk* neighbor = GetChunk(coord.x + d.dx, coord.y + d.dy, coord.z + d.dz);
        if (neighbor) {
            chunk->SetNeighbor(d.face, neighbor);
            neighbor->SetNeighbor(d.opposite, chunk);
        }
    }
}

void ChunkManager::GenerateFlatTerrain(Chunk* chunk) {
    int worldBaseY = chunk->GetChunkY() * Chunk::SIZE;

    for (int y = 0; y < Chunk::SIZE; ++y) {
        int worldY = worldBaseY + y;
        if (worldY > SURFACE_HEIGHT) break;

        BlockType type;
        if (worldY < SURFACE_HEIGHT - 1)
            type = BlockType::Stone;
        else if (worldY == SURFACE_HEIGHT - 1)
            type = BlockType::Dirt;
        else
            type = BlockType::Grass;

        for (int z = 0; z < Chunk::SIZE; ++z)
            for (int x = 0; x < Chunk::SIZE; ++x)
                chunk->SetBlock(x, y, z, type);
    }
}

void ChunkManager::Update(float playerX, float playerZ) {
    int centerX = static_cast<int>(std::floor(playerX / Chunk::SIZE));
    int centerZ = static_cast<int>(std::floor(playerZ / Chunk::SIZE));

    if (centerX == m_lastCenterX && centerZ == m_lastCenterZ)
        return;

    m_lastCenterX = centerX;
    m_lastCenterZ = centerZ;

    // Remove chunks outside render distance
    std::vector<ChunkCoord> toRemove;
    for (auto& [coord, chunk] : m_chunks) {
        if (std::abs(coord.x - centerX) > m_renderDistance ||
            std::abs(coord.z - centerZ) > m_renderDistance) {
            chunk->RemoveFromScene(m_scene);
            toRemove.push_back(coord);
        }
    }
    for (auto& coord : toRemove) {
        delete m_chunks[coord];
        m_chunks.erase(coord);
    }

    // Only y=0 layer for flat world (surface fits in one chunk vertically)
    int cy = 0;

    for (int cx = centerX - m_renderDistance; cx <= centerX + m_renderDistance; ++cx) {
        for (int cz = centerZ - m_renderDistance; cz <= centerZ + m_renderDistance; ++cz) {
            ChunkCoord coord{cx, cy, cz};
            if (m_chunks.count(coord)) continue;

            auto* chunk = new Chunk(cx, cy, cz);
            m_chunks[coord] = chunk;
            GenerateFlatTerrain(chunk);
            LinkNeighbors(coord, chunk);
            chunk->BuildMesh(m_material);
            chunk->AddToScene(m_scene);
        }
    }
}
