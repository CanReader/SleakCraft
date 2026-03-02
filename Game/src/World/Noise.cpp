#include "World/Noise.hpp"
#include <cmath>
#include <algorithm>

Noise::Noise() {
    BuildPermutation();
}

Noise::Noise(uint32_t seed) : m_seed(seed) {
    BuildPermutation();
}

void Noise::SetSeed(uint32_t seed) {
    m_seed = seed;
    BuildPermutation();
}

void Noise::BuildPermutation() {
    uint8_t base[256];
    for (int i = 0; i < 256; ++i)
        base[i] = static_cast<uint8_t>(i);

    // Fisher-Yates shuffle with seed
    uint32_t s = m_seed;
    for (int i = 255; i > 0; --i) {
        // Simple LCG for deterministic shuffle
        s = s * 1664525u + 1013904223u;
        int j = static_cast<int>((s >> 16) % (i + 1));
        std::swap(base[i], base[j]);
    }

    for (int i = 0; i < 256; ++i) {
        m_perm[i] = base[i];
        m_perm[i + 256] = base[i];
    }
}

float Noise::Fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Noise::Lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float Noise::Grad2D(int hash, float x, float y) {
    int h = hash & 3;
    float u = (h & 1) ? x : -x;
    float v = (h & 2) ? y : -y;
    return u + v;
}

float Noise::Grad3D(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float Noise::Perlin2D(float x, float y) const {
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    float u = Fade(xf);
    float v = Fade(yf);

    int aa = m_perm[m_perm[xi] + yi];
    int ab = m_perm[m_perm[xi] + yi + 1];
    int ba = m_perm[m_perm[xi + 1] + yi];
    int bb = m_perm[m_perm[xi + 1] + yi + 1];

    float x1 = Lerp(Grad2D(aa, xf, yf), Grad2D(ba, xf - 1.0f, yf), u);
    float x2 = Lerp(Grad2D(ab, xf, yf - 1.0f), Grad2D(bb, xf - 1.0f, yf - 1.0f), u);

    return Lerp(x1, x2, v);
}

float Noise::Perlin3D(float x, float y, float z) const {
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;
    int zi = static_cast<int>(std::floor(z)) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float zf = z - std::floor(z);

    float u = Fade(xf);
    float v = Fade(yf);
    float w = Fade(zf);

    int a  = m_perm[xi] + yi;
    int aa = m_perm[a] + zi;
    int ab = m_perm[a + 1] + zi;
    int b  = m_perm[xi + 1] + yi;
    int ba = m_perm[b] + zi;
    int bb = m_perm[b + 1] + zi;

    float x1 = Lerp(Grad3D(m_perm[aa], xf, yf, zf),
                     Grad3D(m_perm[ba], xf - 1.0f, yf, zf), u);
    float x2 = Lerp(Grad3D(m_perm[ab], xf, yf - 1.0f, zf),
                     Grad3D(m_perm[bb], xf - 1.0f, yf - 1.0f, zf), u);
    float y1 = Lerp(x1, x2, v);

    float x3 = Lerp(Grad3D(m_perm[aa + 1], xf, yf, zf - 1.0f),
                     Grad3D(m_perm[ba + 1], xf - 1.0f, yf, zf - 1.0f), u);
    float x4 = Lerp(Grad3D(m_perm[ab + 1], xf, yf - 1.0f, zf - 1.0f),
                     Grad3D(m_perm[bb + 1], xf - 1.0f, yf - 1.0f, zf - 1.0f), u);
    float y2 = Lerp(x3, x4, v);

    return Lerp(y1, y2, w);
}

float Noise::FBM2D(float x, float y, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * Perlin2D(x * frequency, y * frequency);
        maxAmplitude += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return sum / maxAmplitude;
}

float Noise::FBM3D(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * Perlin3D(x * frequency, y * frequency, z * frequency);
        maxAmplitude += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return sum / maxAmplitude;
}
