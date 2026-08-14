#ifndef _WORLD_GENERATOR_HPP_
#define _WORLD_GENERATOR_HPP_

#include "Noise.hpp"
#include <cstdint>

class Chunk;

/// Biome classification driven by the temperature/humidity noise layers.
enum class Biome : uint8_t {
    Plains,
    Forest,
    Mountains,
    Desert,
    Beach,
    Ocean
};

/// Per-XZ-column terrain result: surface height and biome.
struct ColumnInfo {
    int surfaceHeight;
    Biome biome;
};

/// Deterministic, seed-driven voxel terrain generator: multi-layer noise for
/// height/biome, spaghetti + cheese cave carving, rivers/lakes, gravel
/// patches, and tree placement. Same seed always produces the same world.
class WorldGenerator {
public:
    static constexpr int MIN_CHUNK_Y = 0;
    static constexpr int MAX_CHUNK_Y = 7;  // blocks 0-127
    static constexpr int SEA_LEVEL = 64;
    static constexpr int BASE_HEIGHT = 64;

    WorldGenerator();
    explicit WorldGenerator(uint32_t seed);

    /// Reseeds every noise layer (see InitNoises for the per-layer offsets).
    void SetSeed(uint32_t seed);
    uint32_t GetSeed() const { return m_seed; }

    /// Fills a chunk's block data: terrain height, biome surface blocks,
    /// caves, rivers/lakes, and (surface chunks only) trees.
    void Generate(Chunk* chunk) const;
    int GetSurfaceHeight(int worldX, int worldZ) const;
    /// Whether a world position falls inside carved-out cave space (combined
    /// spaghetti tunnels and cheese caverns).
    bool IsCave(int worldX, int worldY, int worldZ) const;
    Biome GetBiome(int worldX, int worldZ) const;

    /// True if every block in an already-generated chunk is air.
    bool IsChunkEmpty(const Chunk* chunk) const;
    /// True if every block in an already-generated chunk is solid (no
    /// air/water, meaning no cave or river/lake reached it).
    bool IsChunkFullySolid(const Chunk* chunk) const;
    /// True if a not-yet-generated chunk would end up empty, so it can be
    /// skipped instead of generated and meshed.
    bool IsChunkAboveTerrain(int cx, int cy, int cz) const;
    /// Highest chunk-Y with any terrain/tree content for an XZ column.
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

    /// Seeds each noise layer at m_seed + a fixed per-layer offset (0-9,
    /// 11-13; +10 is reserved for the tree placement hash). New layers must
    /// use +14 or higher.
    void InitNoises();
    /// Scatters trees across a chunk's surface, gated by a per-column hash
    /// so placement is deterministic and independent of generation order.
    void PlaceTrees(Chunk* chunk) const;
    /// Evaluates height/biome noise for one XZ column.
    ColumnInfo GetColumnInfo(int worldX, int worldZ) const;

    /// Deterministic per-cell hash used to gate tree placement.
    static uint32_t HashPosition(int x, int z, uint32_t seed);
};

#endif
