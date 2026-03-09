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

    static AtlasUV GetTileUV(uint8_t tileIndex) {
        int col = tileIndex % TILES_PER_ROW;
        int row = tileIndex / TILES_PER_ROW;
        float tw = 1.0f / static_cast<float>(TILES_PER_ROW);
        float th = 1.0f / static_cast<float>(s_rows);
        return {
            col * tw,       row * th,
            (col + 1) * tw, (row + 1) * th
        };
    }

    static int GetRows() { return s_rows; }

private:
    static int s_rows;
};

#endif
