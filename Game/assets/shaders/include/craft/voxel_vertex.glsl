#ifndef CRAFT_VOXEL_VERTEX_GLSL
#define CRAFT_VOXEL_VERTEX_GLSL

#include "craft/frame.glsl"

// Packed voxel vertex (VoxelVertex in World/VoxelVertex.hpp), vertex stages
// only; the layout below mirrors its VOXEL_* constants
layout(location = 0) in int inVoxelX;
layout(location = 1) in int inVoxelZ;
layout(location = 2) in uint inVoxelYFace;
layout(location = 3) in uint inVoxelTile;

// Fixed point: 256 position units per block
#define CRAFT_VOXEL_UNIT (1.0 / 256.0)
#define CRAFT_VOXEL_Y_BITS 17
#define CRAFT_VOXEL_FACE_SHIFT 17
#define CRAFT_VOXEL_FACE_BITS 4
#define CRAFT_VOXEL_CORNER_SHIFT 21
#define CRAFT_VOXEL_CORNER_BITS 2
#define CRAFT_VOXEL_AO_SHIFT 23
#define CRAFT_VOXEL_AO_BITS 2
#define CRAFT_VOXEL_TILE_BITS 10

// Face normals in BlockFace order
const vec3 CRAFT_VOXEL_NORMALS[] = vec3[](
    vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, -1.0), vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0));
// Same floats as VOXEL_AO_LEVELS
const float CRAFT_VOXEL_AO_LEVELS[] = float[](0.40, 0.68, 0.88, 1.0);
// Tile corner of each UV corner: (u0, v1), (u0, v0), (u1, v0), (u1, v1)
const uvec2 CRAFT_VOXEL_CORNER_UV[] =
    uvec2[](uvec2(0u, 1u), uvec2(0u, 0u), uvec2(1u, 0u), uvec2(1u, 1u));

/// One unsigned field of the Y and face word.
uint CraftVoxelField(int shift, int bits) {
    return bitfieldExtract(inVoxelYFace, shift, bits);
}

/// Position in blocks: world space for chunks, object space for effects.
vec3 CraftVoxelPosition() {
    int y = bitfieldExtract(int(inVoxelYFace), 0, CRAFT_VOXEL_Y_BITS);
    return vec3(ivec3(inVoxelX, y, inVoxelZ)) * CRAFT_VOXEL_UNIT;
}

/// Unit normal of the face.
vec3 CraftVoxelNormal() {
    return CRAFT_VOXEL_NORMALS[CraftVoxelField(CRAFT_VOXEL_FACE_SHIFT,
                                               CRAFT_VOXEL_FACE_BITS)];
}

/// Vertex color: AO brightness in rgb, opaque alpha.
vec4 CraftVoxelColor() {
    float ao = CRAFT_VOXEL_AO_LEVELS[CraftVoxelField(CRAFT_VOXEL_AO_SHIFT,
                                                     CRAFT_VOXEL_AO_BITS)];
    return vec4(ao, ao, ao, 1.0);
}

/// Atlas UV of the tile corner (TextureAtlas::GetGrid): integer texels times
/// the CPU's inverse atlas size, one correctly rounded multiply.
vec2 CraftVoxelUV() {
    uint tile = bitfieldExtract(inVoxelTile, 0, CRAFT_VOXEL_TILE_BITS);
    uint corner =
        CraftVoxelField(CRAFT_VOXEL_CORNER_SHIFT, CRAFT_VOXEL_CORNER_BITS);
    uvec4 grid = uCraft.AtlasGrid;  // tiles per row, cell, pad, tile texels
    uvec2 texel = uvec2(tile % grid.x, tile / grid.x) * grid.y + grid.z +
                  CRAFT_VOXEL_CORNER_UV[corner] * grid.w;
    return vec2(texel) * uCraft.AtlasInvSize.xy;
}

#endif
