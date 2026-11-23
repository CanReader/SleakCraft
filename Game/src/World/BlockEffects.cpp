#include "World/BlockEffects.hpp"

#include <Core/GameObject.hpp>
#include <Core/SceneBase.hpp>
#include <ECS/Components/MaterialComponent.hpp>
#include <ECS/Components/MeshComponent.hpp>
#include <ECS/Components/TransformComponent.hpp>
#include <Runtime/Material.hpp>
#include <algorithm>
#include <cstdlib>

#include "World/QuadIndexBuffer.hpp"
#include "World/VoxelVertex.hpp"

using namespace Sleak;
using namespace Sleak::Math;

namespace {

constexpr float HALF = 0.5f;
// Particle quad: the south face (facing -z, the side LookAt turns to the
// camera) centered on the origin, in packed units
constexpr int32_t PARTICLE_ORIGIN = -VOXEL_POS_UNITS / 2;

/// Uniform random float in [lo, hi).
float RandFloat(float lo, float hi) {
    float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return lo + t * (hi - lo);
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
    for (int f = 0; f < BLOCK_FACE_COUNT; ++f) {
        const BlockFace face = static_cast<BlockFace>(f);
        vertices.AddFace(face, 0, 0, 0, VOXEL_POS_UNITS, VOXEL_AO_UNOCCLUDED,
                         GetBlockTextureTile(type, face), VOXEL_QUAD_START_02);
    }
    return m_cubeMeshes.emplace(type, QuadIndexBuffer::CreateMesh(vertices))
        .first->second;
}

const MeshHandle& BlockEffects::QuadMesh(uint8_t tile) {
    auto it = m_quadMeshes.find(tile);
    if (it != m_quadMeshes.end()) return it->second;

    VoxelVertexBuffer vertices;
    vertices.AddFace(BlockFace::South, PARTICLE_ORIGIN, PARTICLE_ORIGIN, 0,
                     VOXEL_POS_UNITS, VOXEL_AO_UNOCCLUDED, tile,
                     VOXEL_QUAD_START_02);
    return m_quadMeshes.emplace(tile, QuadIndexBuffer::CreateMesh(vertices))
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
