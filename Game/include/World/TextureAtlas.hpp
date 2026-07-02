#ifndef _TEXTURE_ATLAS_HPP_
#define _TEXTURE_ATLAS_HPP_

#include <cstdint>
#include <vector>
#include <string>

namespace Sleak { class Texture; }

struct AtlasUV {
    float u0, v0; // bottom-left
    float u1, v1; // top-right
};

class TextureAtlas {
public:
    static constexpr int TILES_PER_ROW = 4;

    // Build atlas from individual block textures, returns the texture
    // Tile order must match BlockTile enum in Block.hpp
    static Sleak::Texture* BuildAtlas();

    // UVs address the tile content inside its gutter-padded cell
    static AtlasUV GetTileUV(uint8_t tileIndex) {
        int col = tileIndex % TILES_PER_ROW;
        int row = tileIndex / TILES_PER_ROW;
        float x0 = static_cast<float>(col * s_cell + s_pad);
        float y0 = static_cast<float>(row * s_cell + s_pad);
        return {
            x0 * s_invW,            y0 * s_invH,
            (x0 + s_tile) * s_invW, (y0 + s_tile) * s_invH
        };
    }

    static int GetRows() { return s_rows; }

private:
    static int s_rows;
    static int s_tile, s_pad, s_cell;
    static float s_invW, s_invH;
};

#endif
