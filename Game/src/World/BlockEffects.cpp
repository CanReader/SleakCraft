#include "World/BlockEffects.hpp"

#include <Core/GameObject.hpp>
#include <Core/SceneBase.hpp>
#include <ECS/Components/MaterialComponent.hpp>
#include <ECS/Components/MeshComponent.hpp>
#include <ECS/Components/TransformComponent.hpp>
#include <Runtime/Material.hpp>
#include <algorithm>
#include <cstdlib>

#include "World/TextureAtlas.hpp"
#include "World/VoxelVertex.hpp"

using namespace Sleak;
using namespace Sleak::Math;

namespace {

constexpr int QUAD_CORNERS = 4;
constexpr int QUAD_INDICES = 6;

/// One cube face: corners in the chunk mesher's order, normal, atlas face.
struct FaceInfo {
    float positions[QUAD_CORNERS][3];
    float nx, ny, nz;
    BlockFace face;
};

// Same corner order and winding as Chunk::GenerateMeshData
constexpr int CUBE_FACE_COUNT = 6;
constexpr FaceInfo CUBE_FACES[CUBE_FACE_COUNT] = {
    {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, 0, 1, 0, BlockFace::Top},
    {{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, 0, -1, 0, BlockFace::Bottom},
    {{{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, 0, 0, 1, BlockFace::North},
    {{{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}, 0, 0, -1, BlockFace::South},
    {{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, 1, 0, 0, BlockFace::East},
    {{{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}, -1, 0, 0, BlockFace::West},
};
// Front faces of a quad, and the same triangles seen from behind
constexpr uint32_t QUAD_FRONT[QUAD_INDICES] = {0, 2, 1, 0, 3, 2};
constexpr uint32_t QUAD_BACK[QUAD_INDICES] = {0, 1, 2, 0, 2, 3};
constexpr float HALF = 0.5f;
// Unit particle quad on z = 0, corners in the north-face order
constexpr float PARTICLE_CORNERS[QUAD_CORNERS][2] = {
    {HALF, -HALF}, {HALF, HALF}, {-HALF, HALF}, {-HALF, -HALF}};
// Particle sides: front faces toward +z, back faces toward -z
constexpr float PARTICLE_SIDE_NORMALS[] = {1.0f, -1.0f};

/// Uniform random float in [lo, hi).
float RandFloat(float lo, float hi) {
    float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return lo + t * (hi - lo);
}

/// Atlas UVs of a quad's corners in the chunk mesher's order.
void QuadUVs(const AtlasUV& uv, float out[QUAD_CORNERS][2]) {
    out[0][0] = uv.u0;
    out[0][1] = uv.v1;
    out[1][0] = uv.u0;
    out[1][1] = uv.v0;
    out[2][0] = uv.u1;
    out[2][1] = uv.v0;
    out[3][0] = uv.u1;
    out[3][1] = uv.v1;
}

/// Appends a quad's six indices, offset to its first vertex.
void AppendIndices(std::vector<uint32_t>& indices, uint32_t base,
                   const uint32_t (&order)[QUAD_INDICES]) {
    for (uint32_t i : order) indices.push_back(base + i);
}

/// GPU mesh of voxel vertices and indices.
MeshHandle UploadVoxelMesh(const VoxelVertexBuffer& vertices,
                           const std::vector<uint32_t>& indices) {
    return MeshBatch::CreateMesh(GetVoxelVertexFormat(), vertices.GetData(),
                                 vertices.GetSizeInBytes(), indices.data(),
                                 indices.size());
}

}  // namespace

void BlockEffects::Initialize(SceneBase* scene,
                              const RefPtr<Material>& material) {
    m_scene = scene;
    m_material = material;
}

const MeshHandle& BlockEffects::CubeMesh(BlockType type) {
    auto it = m_cubeMeshes.find(type);
    if (it != m_cubeMeshes.end()) return it->second;

    VoxelVertexBuffer vertices;
    std::vector<uint32_t> indices;
    for (const FaceInfo& f : CUBE_FACES) {
        float uvs[QUAD_CORNERS][2];
        QuadUVs(TextureAtlas::GetTileUV(GetBlockTextureTile(type, f.face)),
                uvs);
        const uint32_t base = static_cast<uint32_t>(vertices.GetSize());
        for (int i = 0; i < QUAD_CORNERS; i++) {
            vertices.AddVertex(VoxelVertex(f.positions[i][0], f.positions[i][1],
                                           f.positions[i][2], f.nx, f.ny, f.nz,
                                           uvs[i][0], uvs[i][1]));
        }
        AppendIndices(indices, base, QUAD_FRONT);
    }
    return m_cubeMeshes.emplace(type, UploadVoxelMesh(vertices, indices))
        .first->second;
}

const MeshHandle& BlockEffects::QuadMesh(uint8_t tile) {
    auto it = m_quadMeshes.find(tile);
    if (it != m_quadMeshes.end()) return it->second;

    float uvs[QUAD_CORNERS][2];
    QuadUVs(TextureAtlas::GetTileUV(tile), uvs);
    VoxelVertexBuffer vertices;
    std::vector<uint32_t> indices;
    for (float nz : PARTICLE_SIDE_NORMALS) {
        const uint32_t base = static_cast<uint32_t>(vertices.GetSize());
        for (int i = 0; i < QUAD_CORNERS; i++) {
            vertices.AddVertex(VoxelVertex(PARTICLE_CORNERS[i][0],
                                           PARTICLE_CORNERS[i][1], 0.0f, 0.0f,
                                           0.0f, nz, uvs[i][0], uvs[i][1]));
        }
        AppendIndices(indices, base, nz > 0.0f ? QUAD_FRONT : QUAD_BACK);
    }
    return m_quadMeshes.emplace(tile, UploadVoxelMesh(vertices, indices))
        .first->second;
}

GameObject* BlockEffects::CreateEffectObject(const char* name,
                                             const MeshHandle& mesh) {
    auto* obj = new GameObject(name);
    obj->AddComponent<TransformComponent>(Vector3D(0, 0, 0));
    obj->AddComponent<MaterialComponent>(m_material);
    obj->AddComponent<MeshComponent>(mesh);
    obj->Initialize();
    return obj;
}

void BlockEffects::SpawnPlaceEffect(int x, int y, int z, BlockType type) {
    auto* obj = CreateEffectObject("PlaceEffect", CubeMesh(type));

    const float offset = (1.0f - PLACE_START_SCALE) * HALF;
    if (auto* tr = obj->GetComponent<TransformComponent>()) {
        tr->SetPosition(Vector3D(x + offset, y + offset, z + offset));
        tr->SetScale(
            Vector3D(PLACE_START_SCALE, PLACE_START_SCALE, PLACE_START_SCALE));
    }
    m_scene->AddObject(obj);

    PlaceEffect effect;
    effect.x = x;
    effect.y = y;
    effect.z = z;
    effect.type = type;
    effect.timer = 0.0f;
    effect.duration = PLACE_DURATION;
    effect.obj = obj;
    m_placeEffects.push_back(effect);
}

void BlockEffects::SpawnBreakEffect(int x, int y, int z, BlockType type) {
    const MeshHandle& quad =
        QuadMesh(GetBlockTextureTile(type, BlockFace::South));
    const Vector3D center(static_cast<float>(x) + HALF,
                          static_cast<float>(y) + HALF,
                          static_cast<float>(z) + HALF);

    for (int i = 0; i < PARTICLES_PER_BLOCK; i++) {
        BreakParticle p;
        p.pos = center + Vector3D(RandFloat(-BREAK_SPREAD, BREAK_SPREAD),
                                  RandFloat(-BREAK_SPREAD, BREAK_SPREAD),
                                  RandFloat(-BREAK_SPREAD, BREAK_SPREAD));
        p.vel = Vector3D(RandFloat(-BREAK_SIDE_SPEED, BREAK_SIDE_SPEED),
                         RandFloat(BREAK_UP_SPEED_MIN, BREAK_UP_SPEED_MAX),
                         RandFloat(-BREAK_SIDE_SPEED, BREAK_SIDE_SPEED));
        p.life = 0.0f;
        p.maxLife = BREAK_LIFETIME +
                    RandFloat(-BREAK_LIFETIME_JITTER, BREAK_LIFETIME_JITTER);

        p.obj = CreateEffectObject("BreakParticle", quad);
        if (auto* tr = p.obj->GetComponent<TransformComponent>()) {
            tr->SetPosition(p.pos);
            tr->SetScale(Vector3D(PARTICLE_SIZE, PARTICLE_SIZE, PARTICLE_SIZE));
        }
        m_scene->AddObject(p.obj);
        m_breakParticles.push_back(p);
    }
}

void BlockEffects::Update(float deltaTime, const Vector3D& cameraPos) {
    // Place effects: ease-out scale-in, centered on the block
    for (auto& effect : m_placeEffects) {
        if (!effect.obj) continue;
        effect.timer += deltaTime;
        if (effect.timer >= effect.duration) {
            m_scene->RemoveObject(effect.obj);
            effect.obj = nullptr;
            continue;
        }

        float t = effect.timer / effect.duration;
        t = 1.0f - (1.0f - t) * (1.0f - t);
        const float scale = PLACE_START_SCALE + t * (1.0f - PLACE_START_SCALE);
        const float offset = (1.0f - scale) * HALF;
        if (auto* tr = effect.obj->GetComponent<TransformComponent>()) {
            tr->SetPosition(Vector3D(effect.x + offset, effect.y + offset,
                                     effect.z + offset));
            tr->SetScale(Vector3D(scale, scale, scale));
        }
    }

    // Break particles: gravity, then face the camera
    for (auto& p : m_breakParticles) {
        p.life += deltaTime;
        if (p.life >= p.maxLife) {
            m_scene->RemoveObject(p.obj);
            p.obj = nullptr;
            continue;
        }

        p.vel = p.vel + Vector3D(0, PARTICLE_GRAVITY, 0) * deltaTime;
        p.pos = p.pos + p.vel * deltaTime;
        if (auto* tr = p.obj->GetComponent<TransformComponent>()) {
            tr->SetPosition(p.pos);
            tr->LookAt(cameraPos);
        }
    }
    m_breakParticles.erase(
        std::remove_if(m_breakParticles.begin(), m_breakParticles.end(),
                       [](const BreakParticle& p) { return p.obj == nullptr; }),
        m_breakParticles.end());
}

std::vector<BlockEffects::CompletedPlace>
BlockEffects::PopCompletedPlacements() {
    std::vector<CompletedPlace> result;
    for (auto it = m_placeEffects.begin(); it != m_placeEffects.end();) {
        if (it->obj == nullptr) {
            result.push_back({it->x, it->y, it->z, it->type});
            it = m_placeEffects.erase(it);
        } else {
            ++it;
        }
    }
    return result;
}

std::vector<BlockEffects::CompletedPlace> BlockEffects::DrainAllPlacements() {
    // Commit everything still animating, for the save and exit paths
    std::vector<CompletedPlace> result;
    result.reserve(m_placeEffects.size());
    for (auto& e : m_placeEffects) {
        if (e.obj) m_scene->RemoveObject(e.obj);
        result.push_back({e.x, e.y, e.z, e.type});
    }
    m_placeEffects.clear();
    return result;
}

void BlockEffects::Cleanup() {
    for (auto& e : m_placeEffects) {
        if (e.obj) m_scene->RemoveObject(e.obj);
    }
    for (auto& p : m_breakParticles) {
        if (p.obj) m_scene->RemoveObject(p.obj);
    }
    m_placeEffects.clear();
    m_breakParticles.clear();
}
