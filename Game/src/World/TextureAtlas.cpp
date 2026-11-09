#include "World/TextureAtlas.hpp"

#include <Runtime/Texture.hpp>
#include <UI/UI.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "World/Block.hpp"

using namespace Sleak;
using namespace Sleak::UI;

int TextureAtlas::s_rows = 1;
int TextureAtlas::s_tile = 16;
int TextureAtlas::s_pad  = 8;
int TextureAtlas::s_cell = 32;
float TextureAtlas::s_invW = 1.0f / 128.0f;
float TextureAtlas::s_invH = 1.0f / 160.0f;

// Tile source files in BlockTile enum order
static const char* s_tilePaths[] = {
    "assets/textures/blocks/grass_block_top.png",  // TILE_GRASS_TOP
    "assets/textures/blocks/grass_block_side.png",  // TILE_GRASS_SIDE
    "assets/textures/blocks/dirt.png",              // TILE_DIRT
    "assets/textures/blocks/stone.png",             // TILE_STONE
    "assets/textures/blocks/cobblestone.png",       // TILE_COBBLESTONE
    "assets/textures/blocks/oak_log.png",           // TILE_OAK_LOG
    "assets/textures/blocks/oak_log_top.png",       // TILE_OAK_LOG_TOP
    "assets/textures/blocks/dark_oak_log.png",      // TILE_DARK_OAK_LOG
    "assets/textures/blocks/dark_oak_top.png",      // TILE_DARK_OAK_TOP
    "assets/textures/blocks/spruce_log.png",        // TILE_SPRUCE_LOG
    "assets/textures/blocks/spruce_log_top.png",    // TILE_SPRUCE_LOG_TOP
    "assets/textures/blocks/oak_planks.png",        // TILE_OAK_PLANKS
    "assets/textures/blocks/brick.png",             // TILE_BRICKS
    "assets/textures/blocks/sand.png",              // TILE_SAND
    "assets/textures/blocks/gravel.png",            // TILE_GRAVEL
    "assets/textures/blocks/oak_leaves.png",        // TILE_OAK_LEAVES
    "assets/textures/blocks/water.png",              // TILE_WATER
};

static_assert(sizeof(s_tilePaths) / sizeof(s_tilePaths[0]) == TILE_COUNT,
              "Tile path count must match TILE_COUNT");

namespace {

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
    {TILE_OAK_LEAVES, {61.0f, 88.0f, 24.0f}, false},
};

// Grass fringe texels: green above red and blue by this ratio
constexpr float FRINGE_GREEN_RATIO = 1.15f;
// Texels below this alpha are cut out and stay out of the mean
constexpr unsigned char GRADE_MIN_ALPHA = 128;
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
        if (px[ALPHA] < GRADE_MIN_ALPHA) continue;
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
        tiles[i].pixels = LoadImagePixels(s_tilePaths[i], &tiles[i].w, &tiles[i].h);
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
        const unsigned char* src = tiles[i].pixels;
        if (tiles[i].w != tileSize || tiles[i].h != tileSize) {
            ResampleNearest(tiles[i].pixels, tiles[i].w, tiles[i].h,
                           resampled.data(), tileSize, tileSize);
            src = resampled.data();
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

    // Cap mips so the gutter stays >=1px at the deepest level
    uint32_t maxMips = 1;
    for (int p = pad; p > 1; p >>= 1) ++maxMips;

    Sleak::TextureDesc desc;
    desc.width = static_cast<uint32_t>(atlasW);
    desc.height = static_cast<uint32_t>(atlasH);
    desc.mipLevels = maxMips;
    desc.srgb = true;  // sampled as linear albedo
    const Sleak::TextureMipLevel level0{atlas.data(), atlas.size()};
    return Sleak::CreateTexture(desc, &level0, 1);
}
