#ifndef _QUAD_INDEX_BUFFER_HPP_
#define _QUAD_INDEX_BUFFER_HPP_

#include <Runtime/MeshBatch.hpp>

#include "World/VoxelVertex.hpp"

/// The index buffer every voxel mesh shares, VOXEL_QUAD_PATTERN per quad, so
/// meshes upload their vertices only.
/// @ingroup world
class QuadIndexBuffer {
   public:
    /// Uploads indices for the most quads one column buffer can hold; false
    /// if the upload failed.
    static bool Create();
    /// Drops the buffer; meshes made from it keep it until they are released.
    static void Release();
    /// Uploads whole quads as a voxel-format mesh on the shared buffer.
    static Sleak::MeshHandle CreateMesh(const VoxelVertexBuffer& vertices);
};

#endif
