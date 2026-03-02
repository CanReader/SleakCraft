#include "World/WorldGenerator.hpp"
#include "World/Chunk.hpp"
#include <cmath>

WorldGenerator::WorldGenerator() {
    InitNoises();
}

WorldGenerator::WorldGenerator(uint32_t seed) : m_seed(seed) {
    InitNoises();
}

void WorldGenerator::SetSeed(uint32_t seed) {
    m_seed = seed;
    InitNoises();
}

void WorldGenerator::InitNoises() {
    // Each noise layer gets a different derived seed
    m_continentalness.SetSeed(m_seed);
    m_erosion.SetSeed(m_seed + 1);
    m_detail.SetSeed(m_seed + 2);
    m_cave1.SetSeed(m_seed + 3);
    m_cave2.SetSeed(m_seed + 4);
}

int WorldGenerator::GetSurfaceHeight(int worldX, int worldZ) const {
    float fx = static_cast<float>(worldX);
    float fz = static_cast<float>(worldZ);

    // Continentalness — large-scale hills/mountains (scale ~0.005, 5 octaves)
    float continent = m_continentalness.FBM2D(fx * 0.005f, fz * 0.005f, 5, 2.0f, 0.5f);

    // Erosion — medium-scale variation (scale ~0.01, 3 octaves)
    float erosion = m_erosion.FBM2D(fx * 0.01f, fz * 0.01f, 3, 2.0f, 0.5f);

    // Detail — small bumps (scale ~0.02, 4 octaves)
    float detail = m_detail.FBM2D(fx * 0.02f, fz * 0.02f, 4, 2.0f, 0.5f);

    // Combine: continent controls major shape, erosion modulates amplitude, detail adds texture
    // continent [-1,1] -> terrain amplitude
    float amplitude = 30.0f + continent * 15.0f; // 15-45 block range
    float height = static_cast<float>(BASE_HEIGHT) + continent * amplitude * 0.5f
                   + erosion * 8.0f + detail * 3.0f;

    // Clamp to valid range
    int h = static_cast<int>(std::round(height));
    if (h < 1) h = 1;
    if (h > 126) h = 126;
    return h;
}

bool WorldGenerator::IsCave(int worldX, int worldY, int worldZ) const {
    // Never carve at Y=0 (bedrock layer) or at/above surface
    if (worldY <= 0) return false;

    float fx = static_cast<float>(worldX);
    float fy = static_cast<float>(worldY);
    float fz = static_cast<float>(worldZ);

    // "Swiss cheese" approach — two 3D noise layers
    // Cave exists where both |n1| < threshold AND |n2| < threshold
    float n1 = m_cave1.Perlin3D(fx * 0.04f, fy * 0.04f, fz * 0.04f);
    float n2 = m_cave2.Perlin3D(fx * 0.04f, fy * 0.04f, fz * 0.04f);

    constexpr float threshold = 0.15f;

    return (std::abs(n1) < threshold) && (std::abs(n2) < threshold);
}

void WorldGenerator::Generate(Chunk* chunk) const {
    int chunkBaseX = chunk->GetChunkX() * Chunk::SIZE;
    int chunkBaseY = chunk->GetChunkY() * Chunk::SIZE;
    int chunkBaseZ = chunk->GetChunkZ() * Chunk::SIZE;

    for (int lx = 0; lx < Chunk::SIZE; ++lx) {
        int worldX = chunkBaseX + lx;
        for (int lz = 0; lz < Chunk::SIZE; ++lz) {
            int worldZ = chunkBaseZ + lz;
            int surfaceY = GetSurfaceHeight(worldX, worldZ);

            for (int ly = 0; ly < Chunk::SIZE; ++ly) {
                int worldY = chunkBaseY + ly;

                BlockType type = BlockType::Air;

                if (worldY <= surfaceY) {
                    int depth = surfaceY - worldY;

                    if (depth == 0)
                        type = BlockType::Grass;
                    else if (depth <= 3)
                        type = BlockType::Dirt;
                    else
                        type = BlockType::Stone;

                    // Cave carving — only underground, at least 1 block below surface
                    if (depth >= 1 && IsCave(worldX, worldY, worldZ))
                        type = BlockType::Air;
                }

                chunk->SetBlock(lx, ly, lz, type);
            }
        }
    }
}

bool WorldGenerator::IsChunkEmpty(Chunk* chunk) const {
    const uint8_t* data = chunk->GetBlockData();
    for (int i = 0; i < Chunk::VOLUME; ++i) {
        if (data[i] != static_cast<uint8_t>(BlockType::Air))
            return false;
    }
    return true;
}
