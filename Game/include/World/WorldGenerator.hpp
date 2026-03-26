#ifndef _WORLD_GENERATOR_HPP_
#define _WORLD_GENERATOR_HPP_

#include "Noise.hpp"
#include <cstdint>

class Chunk;

enum class Biome : uint8_t {
    Plains,
    Forest,
    Mountains,
    Desert,
    Beach,
    Ocean
};

struct ColumnInfo {
    int surfaceHeight;
    Biome biome;
};

class WorldGenerator {
public:
    static constexpr int MIN_CHUNK_Y = 0;
    static constexpr int MAX_CHUNK_Y = 7;  // blocks 0-127
    static constexpr int SEA_LEVEL = 64;
    static constexpr int BASE_HEIGHT = 64;

    WorldGenerator();
    explicit WorldGenerator(uint32_t seed);

    void SetSeed(uint32_t seed);
    uint32_t GetSeed() const { return m_seed; }

    void Generate(Chunk* chunk) const;
    int GetSurfaceHeight(int worldX, int worldZ) const;
    bool IsCave(int worldX, int worldY, int worldZ) const;
    Biome GetBiome(int worldX, int worldZ) const;

    bool IsChunkEmpty(const Chunk* chunk) const;
    bool IsChunkFullySolid(const Chunk* chunk) const;
    bool IsChunkAboveTerrain(int cx, int cy, int cz) const;
    int  GetMaxFilledChunkY(int cx, int cz) const;

private:
    Noise m_continentalness;
    Noise m_erosion;
    Noise m_peaksValleys;
    Noise m_detail;
    Noise m_temperature;
    Noise m_humidity;
    Noise m_spaghettiA;
    Noise m_spaghettiB;
    Noise m_cheese;
    Noise m_caveY;
    Noise m_gravelNoise;
    Noise m_riverNoise;
    Noise m_lakeNoise;
    uint32_t m_seed = 0;

    void InitNoises();
    void PlaceTrees(Chunk* chunk) const;
    ColumnInfo GetColumnInfo(int worldX, int worldZ) const;

    static uint32_t HashPosition(int x, int z, uint32_t seed);
};

#endif
