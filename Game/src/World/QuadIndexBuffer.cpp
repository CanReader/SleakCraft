#include "World/QuadIndexBuffer.hpp"

#include <vector>

#include "World/Chunk.hpp"
#include "World/ChunkMesher.hpp"

namespace {

// Every face of every block of a column band: the most one column buffer
// holds, since each block feeds one stream
constexpr uint32_t MAX_QUADS = Chunk::VOLUME * BAND_SIZE * BLOCK_FACE_COUNT;

Sleak::MeshHandle s_indices;

}  // namespace

bool QuadIndexBuffer::Create() {
    std::vector<uint32_t> indices(MAX_QUADS * VOXEL_QUAD_INDICES);
    for (uint32_t q = 0; q < MAX_QUADS; ++q)
        for (uint32_t i = 0; i < VOXEL_QUAD_INDICES; ++i)
            indices[q * VOXEL_QUAD_INDICES + i] =
                q * VOXEL_QUAD_CORNERS + VOXEL_QUAD_PATTERN[i];
    s_indices =
        Sleak::MeshBatch::CreateSharedIndices(indices.data(), indices.size());
    return s_indices.indexBuffer.IsValid();
}

void QuadIndexBuffer::Release() { s_indices = Sleak::MeshHandle(); }

Sleak::MeshHandle QuadIndexBuffer::CreateMesh(
    const VoxelVertexBuffer& vertices) {
    return Sleak::MeshBatch::CreateMesh(
        GetVoxelVertexFormat(), vertices.GetData(), vertices.GetSizeInBytes(),
        s_indices,
        vertices.GetSize() / VOXEL_QUAD_CORNERS * VOXEL_QUAD_INDICES);
}
