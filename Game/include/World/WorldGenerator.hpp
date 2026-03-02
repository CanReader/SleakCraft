#ifndef _WORLD_GENERATOR_HPP_
#define _WORLD_GENERATOR_HPP_

#include "Noise.hpp"
#include <cstdint>

class Chunk;

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

    // Returns true if chunk has any non-air blocks after generation
    bool IsChunkEmpty(Chunk* chunk) const;

private:
    Noise m_continentalness;
    Noise m_erosion;
    Noise m_detail;
    Noise m_cave1;
    Noise m_cave2;
    uint32_t m_seed = 0;

    void InitNoises();
};

#endif
