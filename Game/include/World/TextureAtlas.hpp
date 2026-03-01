#ifndef _TEXTURE_ATLAS_HPP_
#define _TEXTURE_ATLAS_HPP_

#include <cstdint>

struct AtlasUV {
    float u0, v0; // bottom-left
    float u1, v1; // top-right
};

class TextureAtlas {
public:
    static constexpr int TILE_SIZE = 512;
    static constexpr int TILES_PER_ROW = 4;
    static constexpr int ATLAS_WIDTH = TILE_SIZE * TILES_PER_ROW;
    static constexpr int ATLAS_HEIGHT = TILE_SIZE;

    static AtlasUV GetTileUV(uint8_t tileIndex) {
        float tileWidth = 1.0f / TILES_PER_ROW;
        return {
            tileIndex * tileWidth,        0.0f,
            (tileIndex + 1) * tileWidth,  1.0f
        };
    }
};

#endif
