#ifndef _NOISE_HPP_
#define _NOISE_HPP_

#include <cstdint>

class Noise {
public:
    Noise();
    explicit Noise(uint32_t seed);

    void SetSeed(uint32_t seed);
    uint32_t GetSeed() const { return m_seed; }

    float Perlin2D(float x, float y) const;
    float Perlin3D(float x, float y, float z) const;

    float FBM2D(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    float FBM3D(float x, float y, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

private:
    void BuildPermutation();

    static float Fade(float t);
    static float Lerp(float a, float b, float t);
    static float Grad2D(int hash, float x, float y);
    static float Grad3D(int hash, float x, float y, float z);

    uint8_t m_perm[512];
    uint32_t m_seed = 0;
};

#endif
