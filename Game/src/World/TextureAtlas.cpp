#include "World/TextureAtlas.hpp"
#include "World/Block.hpp"
#include <Runtime/Texture.hpp>
#include <UI/UI.hpp>
#include <cstring>
#include <algorithm>

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

// Resample src (srcW x srcH) into dst (dstW x dstH) using nearest neighbor
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

Texture* TextureAtlas::BuildAtlas() {
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
    return CreateTextureFromPixels(atlasW, atlasH, atlas.data(), maxMips);
}
