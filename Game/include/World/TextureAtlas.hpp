#ifndef _TEXTURE_ATLAS_HPP_
#define _TEXTURE_ATLAS_HPP_

#include <Memory/RefPtr.hpp>
#include <Runtime/Texture.hpp>
#include <cstdint>
#include <string>
#include <vector>

/// UV rectangle for one atlas tile's content, inside its gutter-padded cell.
struct AtlasUV {
    float u0, v0; // bottom-left
    float u1, v1; // top-right
};

/// Builds a runtime texture atlas from the individual block PNGs, with each
/// tile wrapped into a gutter-padded cell so linear/aniso filtering never
/// bleeds into a neighboring tile.
/// @ingroup world
class TextureAtlas {
public:
    static constexpr int TILES_PER_ROW = 4;

    /// Loads every block texture in BlockTile order, packs the gutter-padded
    /// atlas and uploads it with its mip chain; null if the upload fails.
    static Sleak::RefPtr<Sleak::Texture> BuildAtlas();

    /// UVs address the tile content inside its gutter-padded cell.
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
