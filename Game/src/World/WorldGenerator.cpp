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
    m_continentalness.SetSeed(m_seed);
    m_erosion.SetSeed(m_seed + 1);
    m_peaksValleys.SetSeed(m_seed + 2);
    m_detail.SetSeed(m_seed + 3);
    m_temperature.SetSeed(m_seed + 4);
    m_humidity.SetSeed(m_seed + 5);
    m_spaghettiA.SetSeed(m_seed + 6);
    m_spaghettiB.SetSeed(m_seed + 7);
    m_cheese.SetSeed(m_seed + 8);
    m_caveY.SetSeed(m_seed + 9);
    // seed +10 reserved for tree hash
    m_gravelNoise.SetSeed(m_seed + 11);
}

uint32_t WorldGenerator::HashPosition(int x, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}

// Single function that computes both height and biome from shared noise values
ColumnInfo WorldGenerator::GetColumnInfo(int worldX, int worldZ) const {
    float fx = static_cast<float>(worldX);
    float fz = static_cast<float>(worldZ);

    // Shared between height and biome
    float continent = m_continentalness.FBM2D(fx * 0.0015f, fz * 0.0015f, 5, 2.0f, 0.5f);
    float erosion   = m_erosion.FBM2D(fx * 0.004f, fz * 0.004f, 4, 2.0f, 0.5f);

    // Biome-only noises
    float temp  = m_temperature.FBM2D(fx * 0.002f, fz * 0.002f, 3, 2.0f, 0.5f);
    float humid = m_humidity.FBM2D(fx * 0.002f, fz * 0.002f, 3, 2.0f, 0.5f);

    // Height-only noises
    float pv     = m_peaksValleys.FBM2D(fx * 0.008f, fz * 0.008f, 5, 2.0f, 0.5f);
    float detail = m_detail.FBM2D(fx * 0.025f, fz * 0.025f, 3, 2.0f, 0.5f);

    // --- Biome ---
    Biome biome;
    if (erosion > 0.2f && continent > 0.3f)
        biome = Biome::Mountains;
    else if (temp > 0.3f && humid < -0.1f)
        biome = Biome::Desert;
    else if (humid > 0.1f)
        biome = Biome::Forest;
    else
        biome = Biome::Plains;

    // --- Height ---
    float base;
    if (continent < -0.2f) {
        float t = (continent + 1.0f) / 0.8f;
        base = 52.0f + t * 12.0f;
    } else if (continent <= 0.3f) {
        float t = (continent + 0.2f) / 0.5f;
        base = 64.0f + t * 8.0f;
    } else {
        float t = (continent - 0.3f) / 0.7f;
        if (t > 1.0f) t = 1.0f;
        base = 72.0f + t * 18.0f;
    }

    float roughness = (erosion + 1.0f) * 0.5f;
    if (roughness < 0.0f) roughness = 0.0f;
    if (roughness > 1.0f) roughness = 1.0f;

    float ridged = 1.0f - std::abs(pv);
    ridged = ridged * ridged;
    float peakContrib = ridged * roughness * 25.0f;
    float gentleContrib = pv * (1.0f - roughness) * 4.0f;
    float detailContrib = detail * 3.0f;

    float height = base + peakContrib + gentleContrib + detailContrib;
    int h = static_cast<int>(std::round(height));
    if (h < 1) h = 1;
    if (h > 126) h = 126;

    return {h, biome};
}

// Public convenience wrappers (for external callers like IsChunkAboveTerrain)
Biome WorldGenerator::GetBiome(int worldX, int worldZ) const {
    return GetColumnInfo(worldX, worldZ).biome;
}

int WorldGenerator::GetSurfaceHeight(int worldX, int worldZ) const {
    return GetColumnInfo(worldX, worldZ).surfaceHeight;
}

bool WorldGenerator::IsCave(int worldX, int worldY, int worldZ) const {
    if (worldY <= 0) return false;

    float fx = static_cast<float>(worldX);
    float fy = static_cast<float>(worldY);
    float fz = static_cast<float>(worldZ);

    // Spaghetti caves: early out on first noise before computing second
    float sa = m_spaghettiA.FBM3D(fx * 0.03f, fy * 0.03f, fz * 0.03f, 2, 2.0f, 0.5f);

    float depthFactor = 1.0f + static_cast<float>(64 - worldY) / 128.0f;
    if (depthFactor < 0.8f) depthFactor = 0.8f;
    float threshold = 0.12f * depthFactor;
    float threshSq = threshold * threshold;

    // Early out: if sa alone exceeds threshold, can't be a spaghetti cave
    if (sa * sa < threshSq) {
        float sb = m_spaghettiB.FBM3D(fx * 0.03f, fy * 0.03f, fz * 0.03f, 2, 2.0f, 0.5f);
        if (sa * sa + sb * sb < threshSq)
            return true;
    }

    // Cheese caves: large caverns below Y=55
    if (worldY < 55) {
        float ch = m_cheese.FBM3D(fx * 0.015f, fy * 0.015f, fz * 0.015f, 3, 2.0f, 0.5f);
        float yFalloff = static_cast<float>(55 - worldY) / 35.0f;
        if (yFalloff > 1.0f) yFalloff = 1.0f;
        if (ch + yFalloff * 0.3f > 0.6f)
            return true;
    }

    return false;
}

void WorldGenerator::PlaceTrees(Chunk* chunk) const {
    int chunkBaseX = chunk->GetChunkX() * Chunk::SIZE;
    int chunkBaseY = chunk->GetChunkY() * Chunk::SIZE;
    int chunkBaseZ = chunk->GetChunkZ() * Chunk::SIZE;
    int chunkTopY  = chunkBaseY + Chunk::SIZE - 1;

    constexpr int CELL_SIZE = 7;
    constexpr int SCAN_RADIUS = 3;
    constexpr int LEAF_RADIUS = 2;

    int cellMinX = (chunkBaseX - SCAN_RADIUS * CELL_SIZE) / CELL_SIZE - 1;
    int cellMaxX = (chunkBaseX + Chunk::SIZE + SCAN_RADIUS * CELL_SIZE) / CELL_SIZE + 1;
    int cellMinZ = (chunkBaseZ - SCAN_RADIUS * CELL_SIZE) / CELL_SIZE - 1;
    int cellMaxZ = (chunkBaseZ + Chunk::SIZE + SCAN_RADIUS * CELL_SIZE) / CELL_SIZE + 1;

    for (int cellX = cellMinX; cellX <= cellMaxX; ++cellX) {
        for (int cellZ = cellMinZ; cellZ <= cellMaxZ; ++cellZ) {
            uint32_t h = HashPosition(cellX, cellZ, m_seed + 10);

            int treeX = cellX * CELL_SIZE + static_cast<int>(h % CELL_SIZE);
            int treeZ = cellZ * CELL_SIZE + static_cast<int>((h >> 8) % CELL_SIZE);

            // Use combined query to avoid redundant noise evaluations
            ColumnInfo col = GetColumnInfo(treeX, treeZ);

            // Biome density check
            float density;
            switch (col.biome) {
                case Biome::Forest:    density = 0.85f; break;
                case Biome::Plains:    density = 0.15f; break;
                case Biome::Mountains: density = 0.25f; break;
                case Biome::Desert:    density = 0.0f;  break;
            }

            float roll = static_cast<float>(h >> 16) / 65535.0f;
            if (roll >= density) continue;

            // Mountains: no trees above Y=90
            if (col.biome == Biome::Mountains && col.surfaceHeight > 90) continue;

            int trunkHeight = 4 + static_cast<int>((h >> 4) % 3);
            int trunkBase = col.surfaceHeight + 1;
            int trunkTop  = trunkBase + trunkHeight - 1;
            int treeTop = trunkTop + 1;

            // Skip if tree doesn't intersect this chunk vertically
            if (trunkBase > chunkTopY || treeTop < chunkBaseY) continue;

            bool isSpruce = (col.biome == Biome::Forest) && ((h >> 12) & 1);
            BlockType logType  = isSpruce ? BlockType::SpruceLog : BlockType::OakLog;
            BlockType leafType = BlockType::OakLeaves;

            // Place trunk
            for (int y = trunkBase; y <= trunkTop; ++y) {
                int ly = y - chunkBaseY;
                if (ly < 0 || ly >= Chunk::SIZE) continue;
                int lx = treeX - chunkBaseX;
                int lz = treeZ - chunkBaseZ;
                if (lx < 0 || lx >= Chunk::SIZE || lz < 0 || lz >= Chunk::SIZE) continue;

                if (chunk->GetBlock(lx, ly, lz) == BlockType::Air)
                    chunk->SetBlock(lx, ly, lz, logType);
            }

            // Place leaves
            int leafStart = trunkBase + trunkHeight / 2;
            for (int y = leafStart; y <= trunkTop + 1; ++y) {
                int radius = (y <= trunkTop) ? LEAF_RADIUS : 1;
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        if (std::abs(dx) == radius && std::abs(dz) == radius) continue;

                        int lx = treeX + dx - chunkBaseX;
                        int ly = y - chunkBaseY;
                        int lz = treeZ + dz - chunkBaseZ;

                        if (lx < 0 || lx >= Chunk::SIZE) continue;
                        if (ly < 0 || ly >= Chunk::SIZE) continue;
                        if (lz < 0 || lz >= Chunk::SIZE) continue;

                        if (chunk->GetBlock(lx, ly, lz) == BlockType::Air)
                            chunk->SetBlock(lx, ly, lz, leafType);
                    }
                }
            }
        }
    }
}

void WorldGenerator::Generate(Chunk* chunk) const {
    int chunkBaseX = chunk->GetChunkX() * Chunk::SIZE;
    int chunkBaseY = chunk->GetChunkY() * Chunk::SIZE;
    int chunkBaseZ = chunk->GetChunkZ() * Chunk::SIZE;

    // Quick reject with increased margin for trees
    int maxH = 0;
    int samples[][2] = {
        {chunkBaseX, chunkBaseZ},
        {chunkBaseX + Chunk::SIZE - 1, chunkBaseZ},
        {chunkBaseX, chunkBaseZ + Chunk::SIZE - 1},
        {chunkBaseX + Chunk::SIZE - 1, chunkBaseZ + Chunk::SIZE - 1},
        {chunkBaseX + Chunk::SIZE / 2, chunkBaseZ + Chunk::SIZE / 2}
    };
    for (auto& s : samples) {
        int h = GetSurfaceHeight(s[0], s[1]);
        if (h > maxH) maxH = h;
    }
    maxH += 16;
    if (chunkBaseY > maxH) return;

    for (int lx = 0; lx < Chunk::SIZE; ++lx) {
        int worldX = chunkBaseX + lx;
        for (int lz = 0; lz < Chunk::SIZE; ++lz) {
            int worldZ = chunkBaseZ + lz;

            // Single noise evaluation for both height and biome
            ColumnInfo col = GetColumnInfo(worldX, worldZ);

            for (int ly = 0; ly < Chunk::SIZE; ++ly) {
                int worldY = chunkBaseY + ly;

                BlockType type = BlockType::Air;

                if (worldY <= col.surfaceHeight) {
                    int depth = col.surfaceHeight - worldY;

                    switch (col.biome) {
                        case Biome::Desert:
                            if (depth <= 4)
                                type = BlockType::Sand;
                            else
                                type = BlockType::Stone;
                            break;

                        case Biome::Mountains:
                            if (col.surfaceHeight > 90) {
                                type = BlockType::Stone;
                            } else {
                                if (depth == 0)
                                    type = BlockType::Grass;
                                else if (depth <= 2)
                                    type = BlockType::Dirt;
                                else
                                    type = BlockType::Stone;
                            }
                            break;

                        case Biome::Plains:
                        case Biome::Forest:
                        default:
                            if (col.surfaceHeight <= SEA_LEVEL + 2 && col.surfaceHeight >= SEA_LEVEL - 2) {
                                if (depth <= 3)
                                    type = BlockType::Sand;
                                else
                                    type = BlockType::Stone;
                            } else {
                                if (depth == 0)
                                    type = BlockType::Grass;
                                else if (depth <= 3)
                                    type = BlockType::Dirt;
                                else
                                    type = BlockType::Stone;
                            }
                            break;
                    }

                    // Underground gravel patches
                    if (type == BlockType::Stone && depth > 10) {
                        float gn = m_gravelNoise.FBM3D(
                            static_cast<float>(worldX) * 0.08f,
                            static_cast<float>(worldY) * 0.08f,
                            static_cast<float>(worldZ) * 0.08f,
                            2, 2.0f, 0.5f);
                        if (gn > 0.55f)
                            type = BlockType::Gravel;
                    }

                    // Cave carving
                    if (depth >= 1 && IsCave(worldX, worldY, worldZ))
                        type = BlockType::Air;
                }

                chunk->SetBlock(lx, ly, lz, type);
            }
        }
    }

    PlaceTrees(chunk);
}

bool WorldGenerator::IsChunkEmpty(const Chunk* chunk) const {
    const uint8_t* data = chunk->GetBlockData();
    for (int i = 0; i < Chunk::VOLUME; ++i) {
        if (data[i] != static_cast<uint8_t>(BlockType::Air))
            return false;
    }
    return true;
}

bool WorldGenerator::IsChunkFullySolid(const Chunk* chunk) const {
    const uint8_t* data = chunk->GetBlockData();
    for (int i = 0; i < Chunk::VOLUME; ++i) {
        if (!IsBlockSolid(static_cast<BlockType>(data[i])))
            return false;
    }
    return true;
}

int WorldGenerator::GetMaxFilledChunkY(int cx, int cz) const {
    int baseX = cx * Chunk::SIZE;
    int baseZ = cz * Chunk::SIZE;
    int maxH = 0;
    int xs[] = {baseX, baseX + Chunk::SIZE - 1, baseX + Chunk::SIZE / 2};
    int zs[] = {baseZ, baseZ + Chunk::SIZE - 1, baseZ + Chunk::SIZE / 2};
    for (int x : xs)
        for (int z : zs) {
            int h = GetSurfaceHeight(x, z);
            if (h > maxH) maxH = h;
        }
    // +16 margin for trees (same as IsChunkAboveTerrain), convert to chunk Y
    int maxCy = (maxH + 16) / Chunk::SIZE;
    if (maxCy > MAX_CHUNK_Y) maxCy = MAX_CHUNK_Y;
    return maxCy;
}

bool WorldGenerator::IsChunkAboveTerrain(int cx, int cy, int cz) const {
    int baseY = cy * Chunk::SIZE;
    if (baseY <= 0) return false;

    int baseX = cx * Chunk::SIZE;
    int baseZ = cz * Chunk::SIZE;

    int maxH = 0;
    int xs[] = {baseX, baseX + Chunk::SIZE - 1, baseX + Chunk::SIZE / 2};
    int zs[] = {baseZ, baseZ + Chunk::SIZE - 1, baseZ + Chunk::SIZE / 2};
    for (int x : xs)
        for (int z : zs) {
            int h = GetSurfaceHeight(x, z);
            if (h > maxH) maxH = h;
        }
    return baseY > maxH + 16;
}
