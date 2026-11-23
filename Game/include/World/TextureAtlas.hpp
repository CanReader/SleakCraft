#ifndef _TEXTURE_ATLAS_HPP_
#define _TEXTURE_ATLAS_HPP_

#include <Memory/RefPtr.hpp>
#include <Runtime/Texture.hpp>
#include <cstdint>
#include <string>
#include <vector>

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

    /// Cell layout in texels plus the inverse atlas size.
    struct Grid {
        uint32_t tilesPerRow;
        uint32_t cellTexels;
        uint32_t padTexels;
        uint32_t tileTexels;
        float invWidth;
        float invHeight;
    };

    /// Layout of the built atlas; the voxel shaders rebuild tile UVs from it.
    static Grid GetGrid() {
        return {TILES_PER_ROW,
                static_cast<uint32_t>(s_cell),
                static_cast<uint32_t>(s_pad),
                static_cast<uint32_t>(s_tile),
                s_invW,
                s_invH};
    }

    static int GetRows() { return s_rows; }

private:
    static int s_rows;
    static int s_tile, s_pad, s_cell;
    static float s_invW, s_invH;
};

#endif
