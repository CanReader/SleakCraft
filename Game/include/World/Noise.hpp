#ifndef _NOISE_HPP_
#define _NOISE_HPP_

#include <cstdint>

/// Seeded Perlin noise generator with 2D/3D fractal Brownian motion (FBM)
/// on top; the terrain and cave generators sample this per world/chunk.
/// @ingroup world
class Noise {
public:
    Noise();
    explicit Noise(uint32_t seed);

    /// Reseeds and rebuilds the permutation table.
    void SetSeed(uint32_t seed);
    uint32_t GetSeed() const { return m_seed; }

    float Perlin2D(float x, float y) const;
    float Perlin3D(float x, float y, float z) const;

    /// Sums octaves of Perlin2D at increasing frequency/decreasing amplitude,
    /// normalized so the result stays in roughly [-1, 1].
    float FBM2D(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    /// 3D counterpart of FBM2D.
    float FBM3D(float x, float y, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

private:
    /// Fisher-Yates shuffles a 0-255 identity table with a seeded LCG and
    /// duplicates it to 512 entries to avoid wrap-around checks in Perlin*D.
    void BuildPermutation();

    static float Fade(float t);
    static float Lerp(float a, float b, float t);
    /// Maps a permutation hash to one of 4 gradient directions and dots it
    /// with the local offset.
    static float Grad2D(int hash, float x, float y);
    /// 3D counterpart of Grad2D, over 12 gradient directions.
    static float Grad3D(int hash, float x, float y, float z);

    uint8_t m_perm[512];
    uint32_t m_seed = 0;
};

#endif
