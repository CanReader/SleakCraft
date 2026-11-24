#ifndef _CHUNK_VISIBILITY_GRAPH_HPP_
#define _CHUNK_VISIBILITY_GRAPH_HPP_

#include <bitset>
#include <cstdint>
#include <vector>

#include "World/Block.hpp"
#include "World/Chunk.hpp"
#include "World/WorldGenerator.hpp"

/// Bit of the face pair (a, b), a < b in BlockFace order, in a face-pair
/// mask.
constexpr int FacePairBit(int a, int b) {
    return a * BLOCK_FACE_COUNT - a * (a + 1) / 2 + (b - a - 1);
}
// The last face pair is the top bit of FULL_FACE_MASK
static_assert(
    FULL_FACE_MASK ==
    (1u << (FacePairBit(BLOCK_FACE_COUNT - 2, BLOCK_FACE_COUNT - 1) + 1)) - 1);

/// True when a face-pair mask connects two different faces.
constexpr bool FacesConnect(uint16_t mask, int a, int b) {
    if (a == b) return false;
    const int bit = a < b ? FacePairBit(a, b) : FacePairBit(b, a);
    return (mask >> bit & 1) != 0;
}

/// One bit per cell of a chunk, at x + z * SIZE + y * SIZE * SIZE.
using ChunkCells = std::bitset<Chunk::VOLUME>;

/// Face-pair mask of the 6-connected components of a chunk's open cells.
uint16_t FaceConnectivity(const ChunkCells& open);

/// Masks and covered-water inputs of a chunk from its blocks, in the
/// Chunk block layout.
Chunk::Visibility ComputeChunkVisibility(const uint8_t* blocks);

/// Picks a chunk's covered mask from the columns entering it from above
/// (bit set: air above the water there), then moves openAbove below the
/// chunk. openWater reports water under air in the chunk.
uint16_t ResolveCoveredWater(const Chunk::Visibility& vis,
                             Chunk::Columns& openAbove, bool& openWater);

/// Minecraft-style cave culling over the loaded square: a walk from the
/// camera's chunk that never steps back toward it along an axis and leaves
/// a chunk only through a face its open cells connect to an entry face.
/// Covered water is closed unless the camera is in water or open water lies
/// in the chunk or a face neighbor.
/// @ingroup world
class ChunkVisibilityGraph {
   public:
    /// Chunk layers of the world; the grid adds one air layer above them.
    static constexpr int WORLD_LAYERS =
        WorldGenerator::MAX_CHUNK_Y - WorldGenerator::MIN_CHUNK_Y + 1;
    /// Grid layers: the world's chunks, then air for cameras above it.
    static constexpr int LAYERS = WORLD_LAYERS + 1;

    /// Masks of one grid chunk; air until filled.
    struct Cell {
        /// Water like air.
        uint16_t seeThrough = FULL_FACE_MASK;
        /// Covered water closed.
        uint16_t covered = FULL_FACE_MASK;
        /// Holds water under air.
        bool openWater = false;
    };

    /// Clears the grid to air over the square of radius columns around
    /// column (centerX, centerZ).
    void Reset(int centerX, int centerZ, int radius);
    /// Grid chunk at a chunk coordinate, or nullptr outside the grid.
    Cell* At(int cx, int cy, int cz);
    /// Walks from the camera's chunk, layer cameraChunkY of Reset's center
    /// column, through the columns within sqrt(reachSq) in XZ of some point
    /// of that chunk, so the result holds wherever the camera is in it.
    void Run(int cameraChunkY, bool waterCamera, float reachSq);
    /// World chunks of a column the last Run reached, bit per chunk Y.
    uint8_t Reached(int cx, int cz) const;

   private:
    /// Grid index of a chunk in grid coordinates.
    int Index(int lx, int y, int lz) const {
        return lx + lz * m_width + y * m_width * m_width;
    }
    /// Mask a walk through grid chunk index crosses.
    uint16_t MaskAt(int index, bool waterCamera) const;

    int m_centerX = 0;
    int m_centerZ = 0;
    int m_radius = 0;
    int m_width = 1;
    std::vector<Cell> m_cells;
    /// Directions the walk entered each chunk by, bit per BlockFace.
    std::vector<uint8_t> m_sources;
    std::vector<uint8_t> m_reached;
    std::vector<uint8_t> m_inReach;
    std::vector<int> m_queue;
};

#endif
