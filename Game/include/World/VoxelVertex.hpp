#ifndef _VOXELVERTEX_HPP_
#define _VOXELVERTEX_HPP_

#include <Runtime/VertexLayout.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

/// Compact 48-byte chunk vertex: position, normal, color/AO, texcoord.
/// Layout must stay byte-identical to the registered vertex format below.
/// @ingroup world
struct VoxelVertex {
    float px, py, pz;     // position  (offset  0, 12 bytes)
    float nx, ny, nz;     // normal    (offset 12, 12 bytes)
    float r, g, b, a;     // color/AO  (offset 24, 16 bytes)
    float u, v;           // texcoord  (offset 40,  8 bytes)
                          // total:              48 bytes

    VoxelVertex() = default;

    VoxelVertex(float px, float py, float pz,
                float nx, float ny, float nz,
                float u = 0.0f, float v = 0.0f)
        : px(px), py(py), pz(pz),
          nx(nx), ny(ny), nz(nz),
          r(1.0f), g(1.0f), b(1.0f), a(1.0f),
          u(u), v(v) {
    }

    void SetColor(float r, float g, float b, float a) {
        this->r = r; this->g = g; this->b = b; this->a = a;
    }

    void SetTexCoord(float u, float v) {
        this->u = u; this->v = v;
    }

    static constexpr size_t GetSize() {
        return sizeof(VoxelVertex);
    }
};

// Layout contract with the registered format below and the voxel shaders
static_assert(sizeof(VoxelVertex) == 48);
static_assert(offsetof(VoxelVertex, px) == 0);
static_assert(offsetof(VoxelVertex, nx) == 12);
static_assert(offsetof(VoxelVertex, r) == 24);
static_assert(offsetof(VoxelVertex, u) == 40);

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

/// Growable CPU-side buffer of voxel quads, uploaded by
/// QuadIndexBuffer::CreateMesh.
class VoxelVertexBuffer {
public:
    VoxelVertexBuffer() = default;

    /// Appends a quad's corners from corner start on; the shared index
    /// pattern splits it between corners start + 1 and start + 3.
    void AddQuad(const VoxelVertex (&quad)[VOXEL_QUAD_CORNERS],
                 uint32_t start) {
        for (uint32_t i = 0; i < VOXEL_QUAD_CORNERS; ++i)
            m_vertices.push_back(quad[(start + i) % VOXEL_QUAD_CORNERS]);
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
            {0, Sleak::VertexAttribFormat::Float3,
             static_cast<uint32_t>(offsetof(VoxelVertex, px))},
            {1, Sleak::VertexAttribFormat::Float3,
             static_cast<uint32_t>(offsetof(VoxelVertex, nx))},
            {2, Sleak::VertexAttribFormat::Float4,
             static_cast<uint32_t>(offsetof(VoxelVertex, r))},
            {3, Sleak::VertexAttribFormat::Float2,
             static_cast<uint32_t>(offsetof(VoxelVertex, u))},
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
