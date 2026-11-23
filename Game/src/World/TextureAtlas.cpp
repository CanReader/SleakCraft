#include "World/TextureAtlas.hpp"

#include <Core/Logger.hpp>
#include <Runtime/Texture.hpp>
#include <UI/UI.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

#include "World/Block.hpp"

using namespace Sleak;
using namespace Sleak::UI;

int TextureAtlas::s_rows = 1;
int TextureAtlas::s_tile = 16;
int TextureAtlas::s_pad  = 8;
int TextureAtlas::s_cell = 32;
float TextureAtlas::s_invW = 1.0f / 128.0f;
float TextureAtlas::s_invH = 1.0f / 160.0f;

namespace {

/// How a tile's alpha behaves in the atlas mips.
enum class TileAlpha {
    Plain,   // opaque or blended, uploaded as loaded
    Cutout,  // alpha tested; its mips keep level 0's coverage
};

/// One atlas tile: the BlockTile it serves, its PNG and its alpha behavior.
struct TileSource {
    BlockTile tile;
    const char* file;  // in BLOCK_TEXTURE_DIR
    TileAlpha alpha;
};

constexpr const char* BLOCK_TEXTURE_DIR = "assets/textures/blocks/";

// Atlas tiles in BlockTile order; the asserts below enforce it
constexpr TileSource TILE_SOURCES[] = {
    {TILE_GRASS_TOP, "grass_block_top.png", TileAlpha::Plain},
    {TILE_GRASS_SIDE, "grass_block_side.png", TileAlpha::Plain},
    {TILE_DIRT, "dirt.png", TileAlpha::Plain},
    {TILE_STONE, "stone.png", TileAlpha::Plain},
    {TILE_COBBLESTONE, "cobblestone.png", TileAlpha::Plain},
    {TILE_OAK_LOG, "oak_log.png", TileAlpha::Plain},
    {TILE_OAK_LOG_TOP, "oak_log_top.png", TileAlpha::Plain},
    {TILE_DARK_OAK_LOG, "dark_oak_log.png", TileAlpha::Plain},
    {TILE_DARK_OAK_TOP, "dark_oak_top.png", TileAlpha::Plain},
    {TILE_SPRUCE_LOG, "spruce_log.png", TileAlpha::Plain},
    {TILE_SPRUCE_LOG_TOP, "spruce_log_top.png", TileAlpha::Plain},
    {TILE_OAK_PLANKS, "oak_planks.png", TileAlpha::Plain},
    {TILE_BRICKS, "brick.png", TileAlpha::Plain},
    {TILE_SAND, "sand.png", TileAlpha::Plain},
    {TILE_GRAVEL, "gravel.png", TileAlpha::Plain},
    {TILE_OAK_LEAVES, "oak_leaves.png", TileAlpha::Cutout},
    {TILE_WATER, "water.png", TileAlpha::Plain},
};

/// True when every source sits at the index of its own BlockTile.
constexpr bool TileSourcesInOrder() {
    for (size_t i = 0; i < std::size(TILE_SOURCES); ++i)
        if (static_cast<size_t>(TILE_SOURCES[i].tile) != i) return false;
    return true;
}

static_assert(std::size(TILE_SOURCES) == TILE_COUNT,
              "One atlas source per BlockTile");
static_assert(TileSourcesInOrder(), "Atlas sources must follow BlockTile");

/// Per-channel linear-light grade that moves a tile's mean to a target.
struct TileGrade {
    int tile;
    float target[3];  // sRGB 0-255 of the graded texels' mean linear color
    bool fringeOnly;  // only the grass fringe of a side tile
};

// Grass and leaves toward vanilla plains colors under the BSL noon light;
// other tiles keep their colors. Tune against the BSL noon references.
constexpr TileGrade TILE_GRADES[] = {
    {TILE_GRASS_TOP, {66.0f, 89.0f, 45.0f}, false},
    {TILE_GRASS_SIDE, {66.0f, 89.0f, 45.0f}, true},
    {TILE_OAK_LEAVES, {68.0f, 92.0f, 42.0f}, false},
};

// Grass fringe texels: green above red and blue by this ratio
constexpr float FRINGE_GREEN_RATIO = 1.15f;
// Gain range that keeps a bad target from wiping out a channel
constexpr float GRADE_GAIN_MIN = 0.05f;
constexpr float GRADE_GAIN_MAX = 8.0f;
// sRGB transfer (IEC 61966-2-1)
constexpr float SRGB_LINEAR_KNEE = 0.04045f;
constexpr float SRGB_ENCODE_KNEE = 0.0031308f;
constexpr float SRGB_LINEAR_SLOPE = 12.92f;
constexpr float SRGB_SCALE = 1.055f;
constexpr float SRGB_OFFSET = 0.055f;
constexpr float SRGB_GAMMA = 2.4f;
constexpr float CHANNEL_MAX = 255.0f;
constexpr float ROUND_HALF = 0.5f;
constexpr int RGBA = 4;
constexpr int RGB = 3;
constexpr int ALPHA = 3;
constexpr int ALPHA_OPAQUE = std::numeric_limits<unsigned char>::max();
// Voxel shader alpha test (CRAFT_ALPHA_CUTOFF, matAlphaCutoff)
constexpr float ALPHA_CUTOFF = 0.5f;
// Alpha bytes below this are cut out
constexpr int CUTOUT_ALPHA = 128;
static_assert(CUTOUT_ALPHA >= ALPHA_CUTOFF * CHANNEL_MAX &&
                  CUTOUT_ALPHA - 1 < ALPHA_CUTOFF * CHANNEL_MAX,
              "CUTOUT_ALPHA must be the first alpha byte at the cutoff");
// GPU box mips average 2x2 texels
constexpr int BOX_TEXELS = 4;
// Coverage sub-samples per texel edge; their weights are whole multiples of
// 1 / WEIGHT_STEPS
constexpr int SUBSAMPLES = 4;
constexpr int WEIGHT_STEPS = 2 * SUBSAMPLES;

/// Linear light of an sRGB channel in [0, 1].
float SrgbToLinear(float c) {
    return c <= SRGB_LINEAR_KNEE
               ? c / SRGB_LINEAR_SLOPE
               : std::pow((c + SRGB_OFFSET) / SRGB_SCALE, SRGB_GAMMA);
}

/// sRGB encoding of a linear channel, clamped to [0, 1].
float LinearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= SRGB_ENCODE_KNEE
               ? c * SRGB_LINEAR_SLOPE
               : SRGB_SCALE * std::pow(c, 1.0f / SRGB_GAMMA) - SRGB_OFFSET;
}

/// True for the green grass fringe texels of a side tile.
bool IsGrassFringe(const unsigned char* px) {
    return px[1] > px[0] * FRINGE_GREEN_RATIO &&
           px[1] > px[2] * FRINGE_GREEN_RATIO;
}

/// Grades a tile's RGBA8 texels in place, in linear light.
void GradeTile(unsigned char* pixels, int texels, const TileGrade& grade) {
    double sum[RGB] = {};
    int counted = 0;
    for (int i = 0; i < texels; ++i) {
        const unsigned char* px = pixels + i * RGBA;
        if (px[ALPHA] < CUTOUT_ALPHA) continue;
        if (grade.fringeOnly && !IsGrassFringe(px)) continue;
        for (int c = 0; c < RGB; ++c)
            sum[c] += SrgbToLinear(px[c] / CHANNEL_MAX);
        ++counted;
    }
    if (counted == 0) return;

    float gain[RGB];
    for (int c = 0; c < RGB; ++c) {
        const float mean = static_cast<float>(sum[c] / counted);
        const float target = SrgbToLinear(grade.target[c] / CHANNEL_MAX);
        gain[c] = mean > 0.0f ? std::clamp(target / mean, GRADE_GAIN_MIN,
                                           GRADE_GAIN_MAX)
                              : 1.0f;
    }
    for (int i = 0; i < texels; ++i) {
        unsigned char* px = pixels + i * RGBA;
        if (grade.fringeOnly && !IsGrassFringe(px)) continue;
        for (int c = 0; c < RGB; ++c) {
            const float lin = SrgbToLinear(px[c] / CHANNEL_MAX) * gain[c];
            px[c] = static_cast<unsigned char>(LinearToSrgb(lin) * CHANNEL_MAX +
                                               ROUND_HALF);
        }
    }
}

/// Gives every alpha-0 texel the tile's alpha-weighted mean color in linear
/// light, so no mip or bilinear tap pulls in black.
void FillHoles(unsigned char* pixels, int texels) {
    double sum[RGB] = {};
    double weight = 0.0;
    for (int i = 0; i < texels; ++i) {
        const unsigned char* px = pixels + i * RGBA;
        weight += px[ALPHA];
        for (int c = 0; c < RGB; ++c)
            sum[c] += px[ALPHA] * SrgbToLinear(px[c] / CHANNEL_MAX);
    }
    if (weight == 0.0) return;

    unsigned char fill[RGB];
    for (int c = 0; c < RGB; ++c) {
        const float mean = static_cast<float>(sum[c] / weight);
        fill[c] = static_cast<unsigned char>(LinearToSrgb(mean) * CHANNEL_MAX +
                                             ROUND_HALF);
    }
    for (int i = 0; i < texels; ++i) {
        unsigned char* px = pixels + i * RGBA;
        if (px[ALPHA] == 0) memcpy(px, fill, RGB);
    }
}

/// Index i in [-n, 2n) wrapped into [0, n).
int Wrap(int i, int n) { return i < 0 ? i + n : (i < n ? i : i - n); }

/// A tile's alpha with visible alpha scaled to visible / 255, never below
/// CUTOUT_ALPHA, so level 0 keeps its alpha-test mask.
std::vector<int> ScaledAlpha(const unsigned char* pixels, int texels,
                             int visible) {
    std::vector<int> alpha(texels);
    for (int i = 0; i < texels; ++i) {
        const int a = pixels[i * RGBA + ALPHA];
        alpha[i] =
            a < CUTOUT_ALPHA
                ? a
                : std::max(CUTOUT_ALPHA,
                           (a * visible + ALPHA_OPAQUE / 2) / ALPHA_OPAQUE);
    }
    return alpha;
}

/// Alpha of the next GPU mip: the 2x2 box, rounded to nearest.
std::vector<int> HalveAlpha(const std::vector<int>& alpha, int size) {
    const int half = size / 2;
    std::vector<int> out(half * half);
    for (int y = 0; y < half; ++y) {
        for (int x = 0; x < half; ++x) {
            const int* a = &alpha[2 * (y * size + x)];
            out[y * half + x] =
                (a[0] + a[1] + a[size] + a[size + 1] + BOX_TEXELS / 2) /
                BOX_TEXELS;
        }
    }
    return out;
}

/// Fraction of a wrapped alpha grid where bilinear filtering passes the alpha
/// test, from SUBSAMPLES x SUBSAMPLES points per texel.
double BilinearCoverage(const std::vector<int>& alpha, int size) {
    int first[SUBSAMPLES];
    int weight[SUBSAMPLES];
    for (int s = 0; s < SUBSAMPLES; ++s) {
        // Offset from the texel center, in 1 / WEIGHT_STEPS texel
        const int offset = 2 * s + 1 - SUBSAMPLES;
        first[s] = offset < 0 ? -1 : 0;
        weight[s] = offset - first[s] * WEIGHT_STEPS;
    }
    const auto at = [&](int x, int y) {
        return alpha[Wrap(y, size) * size + Wrap(x, size)];
    };
    const float passSum =
        ALPHA_CUTOFF * CHANNEL_MAX * WEIGHT_STEPS * WEIGHT_STEPS;
    int passed = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            for (int sy = 0; sy < SUBSAMPLES; ++sy) {
                for (int sx = 0; sx < SUBSAMPLES; ++sx) {
                    const int x0 = x + first[sx];
                    const int y0 = y + first[sy];
                    const int wx = weight[sx];
                    const int wy = weight[sy];
                    const int top =
                        (WEIGHT_STEPS - wx) * at(x0, y0) + wx * at(x0 + 1, y0);
                    const int bottom = (WEIGHT_STEPS - wx) * at(x0, y0 + 1) +
                                       wx * at(x0 + 1, y0 + 1);
                    if ((WEIGHT_STEPS - wy) * top + wy * bottom >= passSum)
                        ++passed;
                }
            }
        }
    }
    return static_cast<double>(passed) /
           (size * size * SUBSAMPLES * SUBSAMPLES);
}

/// Worst over- and under-coverage of a tile's mips against a target.
struct CoverageError {
    double over;
    double under;
};

/// Coverage error of the GPU box mips once visible alpha is scaled to
/// visible / 255.
CoverageError MipCoverageError(const unsigned char* pixels, int size,
                               uint32_t levels, int visible, double target) {
    std::vector<int> alpha = ScaledAlpha(pixels, size * size, visible);
    double lowest = BilinearCoverage(alpha, size);
    double highest = lowest;
    for (uint32_t level = 1; level < levels; ++level) {
        alpha = HalveAlpha(alpha, size);
        size /= 2;
        const double coverage = BilinearCoverage(alpha, size);
        lowest = std::min(lowest, coverage);
        highest = std::max(highest, coverage);
    }
    return {highest - target, target - lowest};
}

/// Scales a cutout tile's visible alpha so the worst of its GPU box mips stays
/// closest to level 0's alpha-test coverage; returns the visible alpha.
int KeepCutoutCoverage(unsigned char* pixels, int size, uint32_t levels) {
    const int texels = size * size;
    const double target =
        BilinearCoverage(ScaledAlpha(pixels, texels, ALPHA_OPAQUE), size);
    const auto error = [&](int a) {
        return MipCoverageError(pixels, size, levels, a, target);
    };

    // Scaling only thins, so mips that never over-cover stay as they are
    int visible = ALPHA_OPAQUE;
    const CoverageError unscaled = error(ALPHA_OPAQUE);
    if (unscaled.over > unscaled.under) {
        // First alpha whose over-coverage catches up with its under-coverage
        const auto alphas = std::views::iota(CUTOUT_ALPHA, ALPHA_OPAQUE + 1);
        visible = *std::ranges::partition_point(alphas, [&](int a) {
            const CoverageError e = error(a);
            return e.over < e.under;
        });
        if (visible > CUTOUT_ALPHA &&
            error(visible - 1).under < error(visible).over)
            --visible;
    }

    const std::vector<int> alpha = ScaledAlpha(pixels, texels, visible);
    for (int i = 0; i < texels; ++i)
        pixels[i * RGBA + ALPHA] = static_cast<unsigned char>(alpha[i]);
    return visible;
}

}  // namespace

/// Resamples src (srcW x srcH) into dst (dstW x dstH) using nearest neighbor.
static void ResampleNearest(const unsigned char* src, int srcW, int srcH,
                            unsigned char* dst, int dstW, int dstH) {
    for (int y = 0; y < dstH; ++y) {
        int sy = y * srcH / dstH;
        for (int x = 0; x < dstW; ++x) {
            int sx = x * srcW / dstW;
            memcpy(&dst[(y * dstW + x) * 4],
                   &src[(sy * srcW + sx) * 4], 4);
        }
    }
}

Sleak::RefPtr<Sleak::Texture> TextureAtlas::BuildAtlas() {
    constexpr int tileCount = TILE_COUNT;
    s_rows = (tileCount + TILES_PER_ROW - 1) / TILES_PER_ROW;

    // First pass: load all tiles and find the max dimension to use as tile size
    struct TileData {
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
    };
    TileData tiles[TILE_COUNT];

    int tileSize = 0;
    for (int i = 0; i < tileCount; ++i) {
        const std::string path =
            std::string(BLOCK_TEXTURE_DIR) + TILE_SOURCES[i].file;
        tiles[i].pixels =
            LoadImagePixels(path.c_str(), &tiles[i].w, &tiles[i].h);
        if (tiles[i].pixels) {
            tileSize = std::max(tileSize, std::max(tiles[i].w, tiles[i].h));
        }
    }

    // Level-0 grades; the GPU builds the mips from the graded texels
    for (const TileGrade& grade : TILE_GRADES) {
        TileData& t = tiles[grade.tile];
        if (t.pixels) GradeTile(t.pixels, t.w * t.h, grade);
    }

    if (tileSize == 0) tileSize = 16; // fallback

    // Wrapped gutters: each cell holds the tile plus PAD px of the tile
    // wrapped around itself, so linear/aniso taps crossing the tile edge
    // read the seamless continuation instead of the neighboring tile.
    int pad  = tileSize / 2;
    int cell = tileSize + 2 * pad;
    int atlasW = cell * TILES_PER_ROW;
    int atlasH = cell * s_rows;

    s_tile = tileSize;
    s_pad  = pad;
    s_cell = cell;
    s_invW = 1.0f / static_cast<float>(atlasW);
    s_invH = 1.0f / static_cast<float>(atlasH);

    // Cap mips so the gutter stays >=1px at the deepest level
    uint32_t maxMips = 1;
    for (int p = pad; p > 1; p >>= 1) ++maxMips;

    // Allocate atlas pixel buffer (RGBA)
    std::vector<unsigned char> atlas(atlasW * atlasH * 4, 0);

    // Temporary buffer for resampled tiles
    std::vector<unsigned char> resampled(tileSize * tileSize * 4);

    for (int i = 0; i < tileCount; ++i) {
        int col = i % TILES_PER_ROW;
        int row = i / TILES_PER_ROW;
        int offsetX = col * cell;
        int offsetY = row * cell;

        if (!tiles[i].pixels) continue;

        // Resample to tileSize x tileSize if needed
        unsigned char* src = tiles[i].pixels;
        if (tiles[i].w != tileSize || tiles[i].h != tileSize) {
            ResampleNearest(tiles[i].pixels, tiles[i].w, tiles[i].h,
                           resampled.data(), tileSize, tileSize);
            src = resampled.data();
        }

        // Cutout tiles: level 0 shaped for the GPU-built mips
        if (TILE_SOURCES[i].alpha == TileAlpha::Cutout) {
            FillHoles(src, tileSize * tileSize);
            const int visible = KeepCutoutCoverage(src, tileSize, maxMips);
            SLEAK_INFO("Atlas tile {} keeps its mip coverage at alpha {}", i,
                       visible);
        }

        // Fill the whole cell with the tile wrapped (content + gutters)
        for (int y = 0; y < cell; ++y) {
            int sy = ((y - pad) % tileSize + tileSize) % tileSize;
            for (int x = 0; x < cell; ++x) {
                int sx = ((x - pad) % tileSize + tileSize) % tileSize;
                memcpy(&atlas[((offsetY + y) * atlasW + offsetX + x) * 4],
                       &src[(sy * tileSize + sx) * 4], 4);
            }
        }

        FreeImagePixels(tiles[i].pixels);
    }

    Sleak::TextureDesc desc;
    desc.width = static_cast<uint32_t>(atlasW);
    desc.height = static_cast<uint32_t>(atlasH);
    desc.mipLevels = maxMips;
    desc.srgb = true;  // sampled as linear albedo
    const Sleak::TextureMipLevel level0{atlas.data(), atlas.size()};
    return Sleak::CreateTexture(desc, &level0, 1);
}
