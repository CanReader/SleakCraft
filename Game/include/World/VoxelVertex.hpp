#ifndef _VOXELVERTEX_HPP_
#define _VOXELVERTEX_HPP_

#include <Runtime/VertexLayout.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "World/Block.hpp"

/// Vertices per voxel quad.
inline constexpr uint32_t VOXEL_QUAD_CORNERS = 4;
/// Indices per voxel quad.
inline constexpr uint32_t VOXEL_QUAD_INDICES = 6;
/// Indices of quad q are 4q plus these: two triangles sharing stored corners
/// 1 and 3 (QuadIndexBuffer).
inline constexpr uint32_t VOXEL_QUAD_PATTERN[VOXEL_QUAD_INDICES] = {0, 3, 1,
                                                                    1, 3, 2};
/// First stored corner of a quad split along its 0-2 diagonal.
inline constexpr uint32_t VOXEL_QUAD_START_02 = 3;

/// Fixed-point position units per block (1/256 block).
inline constexpr int32_t VOXEL_POS_UNITS = 256;
/// Bits of the signed Y at the bottom of VoxelVertex::yFace.
inline constexpr uint32_t VOXEL_Y_BITS = 17;
/// Face id in yFace: a BlockFace in 4 bits.
inline constexpr uint32_t VOXEL_FACE_SHIFT = 17;
/// UV corner in yFace, 2 bits: (u0, v1), (u0, v0), (u1, v0), (u1, v1).
inline constexpr uint32_t VOXEL_CORNER_SHIFT = 21;
/// AO level in yFace, 2 bits indexing VOXEL_AO_LEVELS; bits 25-31 stay 0.
inline constexpr uint32_t VOXEL_AO_SHIFT = 23;
/// Atlas tile bits at the bottom of VoxelVertex::tile; the rest stay 0.
inline constexpr uint32_t VOXEL_TILE_BITS = 10;

/// Vertex AO brightness per level, the same floats as the voxel shaders.
inline constexpr float VOXEL_AO_LEVELS[] = {0.40f, 0.68f, 0.88f, 1.0f};
/// AO levels of a face nothing occludes: full brightness at every corner.
inline constexpr uint32_t VOXEL_AO_UNOCCLUDED[] = {3, 3, 3, 3};

/// Corners of each block face in BlockFace order, in blocks from the block
/// origin; corner i takes UV corner i.
inline constexpr int32_t
    VOXEL_FACE_CORNERS[BLOCK_FACE_COUNT][VOXEL_QUAD_CORNERS][3] = {
        {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},  // Top
        {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}},  // Bottom
        {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}},  // North
        {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},  // South
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},  // East
        {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},  // West
};

/// 16-byte voxel vertex: fixed-point position and packed attributes,
/// decoded by craft/voxel_vertex.glsl.
/// @ingroup world
struct VoxelVertex {
    int32_t x;       ///< X in VOXEL_POS_UNITS per block
    int32_t z;       ///< Z in VOXEL_POS_UNITS per block
    uint32_t yFace;  ///< signed Y, face, UV corner, AO level
    uint32_t tile;   ///< atlas tile (BlockTile)
};
static_assert(sizeof(VoxelVertex) == 16);
static_assert(TILE_COUNT <= 1 << VOXEL_TILE_BITS);

/// Packs one vertex; positions in VOXEL_POS_UNITS per block.
constexpr VoxelVertex PackVoxelVertex(int32_t x, int32_t y, int32_t z,
                                      BlockFace face, uint32_t corner,
                                      uint32_t ao, uint32_t tile) {
    constexpr uint32_t yMask = (1u << VOXEL_Y_BITS) - 1;
    return {x, z,
            (static_cast<uint32_t>(y) & yMask) |
                (static_cast<uint32_t>(face) << VOXEL_FACE_SHIFT) |
                (corner << VOXEL_CORNER_SHIFT) | (ao << VOXEL_AO_SHIFT),
            tile};
}

/// Y of a packed vertex in blocks, the float the voxel shaders decode.
constexpr float VoxelVertexY(const VoxelVertex& v) {
    constexpr uint32_t unused =
        std::numeric_limits<uint32_t>::digits - VOXEL_Y_BITS;
    return static_cast<float>(static_cast<int32_t>(v.yFace << unused) >>
                              unused) /
           VOXEL_POS_UNITS;
}
// Y keeps its sign: effect meshes reach below their origin
static_assert(VoxelVertexY(PackVoxelVertex(0, -1, 0, BlockFace::Top, 0, 0,
                                           0)) == -1.0f / VOXEL_POS_UNITS);

/// Growable CPU-side buffer of voxel quads, uploaded by
/// QuadIndexBuffer::CreateMesh.
class VoxelVertexBuffer {
public:
    VoxelVertexBuffer() = default;

    /// Appends a block face, origin and top edge height in packed units,
    /// from corner start on: the shared index pattern splits it between
    /// corners start + 1 and start + 3.
    void AddFace(BlockFace face, int32_t x, int32_t y, int32_t z,
                 int32_t height, const uint32_t (&ao)[VOXEL_QUAD_CORNERS],
                 uint32_t tile, uint32_t start) {
        const auto& corners = VOXEL_FACE_CORNERS[static_cast<int>(face)];
        for (uint32_t i = 0; i < VOXEL_QUAD_CORNERS; ++i) {
            const uint32_t c = (start + i) % VOXEL_QUAD_CORNERS;
            m_vertices.push_back(PackVoxelVertex(
                x + corners[c][0] * VOXEL_POS_UNITS, y + corners[c][1] * height,
                z + corners[c][2] * VOXEL_POS_UNITS, face, c, ao[c], tile));
        }
    }

    /// Reserves room for at least count vertices.
    void Reserve(size_t count) { m_vertices.reserve(count); }

    /// Appends count vertices in one copy.
    void Append(const VoxelVertex* vertices, size_t count) {
        m_vertices.insert(m_vertices.end(), vertices, vertices + count);
    }

    const VoxelVertex* GetData() const {
        return m_vertices.data();
    }

    size_t GetSize() const {
        return m_vertices.size();
    }

    size_t GetSizeInBytes() const {
        return m_vertices.size() * sizeof(VoxelVertex);
    }

    void clear() { m_vertices.clear(); }
    void release() { std::vector<VoxelVertex>().swap(m_vertices); }

private:
    std::vector<VoxelVertex> m_vertices;
};

/// Registers the voxel layout on first call and caches its handle.
/// Thread-safe through the function-local static.
inline Sleak::VertexFormatHandle GetVoxelVertexFormat() {
    static const Sleak::VertexFormatHandle handle = [] {
        Sleak::VertexLayoutDesc desc;
        desc.stride = sizeof(VoxelVertex);
        desc.attributes = {
            {0, Sleak::VertexAttribFormat::Int1,
             static_cast<uint32_t>(offsetof(VoxelVertex, x))},
            {1, Sleak::VertexAttribFormat::Int1,
             static_cast<uint32_t>(offsetof(VoxelVertex, z))},
            {2, Sleak::VertexAttribFormat::UInt1,
             static_cast<uint32_t>(offsetof(VoxelVertex, yFace))},
            {3, Sleak::VertexAttribFormat::UInt1,
             static_cast<uint32_t>(offsetof(VoxelVertex, tile))},
        };
        desc.shaderStem = "flat_shader";
        desc.shadowShaderStem = "shadow_depth_voxel";
        desc.gbufferShaderStem = "gbuffer_voxel";
        desc.transparentShaderStem = "water_shader";
        desc.transparentReadsScene = true;
        // Writes RT3 with CRAFT_SHADING_MODEL_VOXEL
        desc.gbufferFragmentStem = "gbuffer_voxel";
        return Sleak::VertexFormatRegistry::Register(desc);
    }();
    return handle;
}

#endif
