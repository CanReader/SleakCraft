#include "World/BlockEffects.hpp"
#include "World/TextureAtlas.hpp"
#include <Core/GameObject.hpp>
#include <Core/SceneBase.hpp>
#include <ECS/Components/TransformComponent.hpp>
#include <ECS/Components/MeshComponent.hpp>
#include <ECS/Components/MaterialComponent.hpp>
#include <Runtime/MeshData.hpp>
#include <Runtime/Material.hpp>
#include <cstdlib>

using namespace Sleak;
using namespace Sleak::Math;

static float RandFloat(float lo, float hi) {
    float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return lo + t * (hi - lo);
}

// Build a cube mesh centered at (0.5, 0.5, 0.5) with size 1 in local space
// Position/scale controlled via TransformComponent
static void BuildUnitCube(uint8_t tileIndex, VertexGroup& verts, IndexGroup& inds) {
    AtlasUV uv = TextureAtlas::GetTileUV(tileIndex);

    // 6 faces, each with 4 vertices — cube spans [0,1] in each axis
    struct FaceData {
        float positions[4][3];
        float nx, ny, nz;
    };

    FaceData faces[6] = {
        // Top
        {{{0,1,0}, {0,1,1}, {1,1,1}, {1,1,0}}, 0, 1, 0},
        // Bottom
        {{{0,0,1}, {0,0,0}, {1,0,0}, {1,0,1}}, 0, -1, 0},
        // North (+Z)
        {{{1,0,1}, {1,1,1}, {0,1,1}, {0,0,1}}, 0, 0, 1},
        // South (-Z)
        {{{0,0,0}, {0,1,0}, {1,1,0}, {1,0,0}}, 0, 0, -1},
        // East (+X)
        {{{1,0,0}, {1,1,0}, {1,1,1}, {1,0,1}}, 1, 0, 0},
        // West (-X)
        {{{0,0,1}, {0,1,1}, {0,1,0}, {0,0,0}}, -1, 0, 0},
    };

    float uvs[4][2] = {
        {uv.u0, uv.v1}, {uv.u0, uv.v0}, {uv.u1, uv.v0}, {uv.u1, uv.v1}
    };

    for (int f = 0; f < 6; f++) {
        uint32_t base = static_cast<uint32_t>(verts.GetSize());
        for (int i = 0; i < 4; i++) {
            Vertex v(faces[f].positions[i][0],
                     faces[f].positions[i][1],
                     faces[f].positions[i][2],
                     faces[f].nx, faces[f].ny, faces[f].nz,
                     1, 0, 0, 1,
                     uvs[i][0], uvs[i][1]);
            v.SetColor(1.0f, 1.0f, 1.0f, 1.0f);
            verts.AddVertex(v);
        }
        inds.add(base);     inds.add(base + 2); inds.add(base + 1);
        inds.add(base);     inds.add(base + 3); inds.add(base + 2);
    }
}

void BlockEffects::Initialize(SceneBase* scene, const RefPtr<Material>& material) {
    m_scene = scene;
    m_material = material;
}

GameObject* BlockEffects::CreatePlaceCube(BlockType type) {
    VertexGroup verts;
    IndexGroup inds;
    uint8_t tile = GetBlockTextureTile(type, BlockFace::South);
    BuildUnitCube(tile, verts, inds);

    MeshData meshData;
    meshData.vertices = std::move(verts);
    meshData.indices = std::move(inds);

    auto* obj = new GameObject("PlaceEffect");
    obj->AddComponent<TransformComponent>(Vector3D(0, 0, 0));
    obj->AddComponent<MaterialComponent>(m_material);
    obj->AddComponent<MeshComponent>(std::move(meshData));
    obj->Initialize();
    return obj;
}

GameObject* BlockEffects::CreateParticleCube(uint8_t tileIndex) {
    VertexGroup verts;
    IndexGroup inds;
    BuildUnitCube(tileIndex, verts, inds);

    MeshData meshData;
    meshData.vertices = std::move(verts);
    meshData.indices = std::move(inds);

    auto* obj = new GameObject("BreakParticle");
    obj->AddComponent<TransformComponent>(Vector3D(0, 0, 0));
    obj->AddComponent<MaterialComponent>(m_material);
    obj->AddComponent<MeshComponent>(std::move(meshData));
    obj->Initialize();
    auto* tr = obj->GetComponent<TransformComponent>();
    if (tr) tr->SetScale(Vector3D(PARTICLE_SIZE, PARTICLE_SIZE, PARTICLE_SIZE));
    return obj;
}

void BlockEffects::SpawnPlaceEffect(int x, int y, int z, BlockType type) {
    auto* obj = CreatePlaceCube(type);

    // Start small, centered on the block position
    float startScale = 0.3f;
    float offset = (1.0f - startScale) * 0.5f;
    auto* tr = obj->GetComponent<TransformComponent>();
    if (tr) {
        tr->SetPosition(Vector3D(x + offset, y + offset, z + offset));
        tr->SetScale(Vector3D(startScale, startScale, startScale));
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
    uint8_t tile = GetBlockTextureTile(type, BlockFace::South);
    float cx = static_cast<float>(x) + 0.5f;
    float cy = static_cast<float>(y) + 0.5f;
    float cz = static_cast<float>(z) + 0.5f;

    for (int i = 0; i < PARTICLES_PER_BLOCK; i++) {
        float px = cx + RandFloat(-0.3f, 0.3f);
        float py = cy + RandFloat(-0.3f, 0.3f);
        float pz = cz + RandFloat(-0.3f, 0.3f);

        auto* obj = CreateParticleCube(tile);
        auto* tr = obj->GetComponent<TransformComponent>();
        if (tr) {
            tr->SetPosition(Vector3D(px, py, pz));
        }
        m_scene->AddObject(obj);

        BreakParticle p;
        p.pos = Vector3D(px, py, pz);
        p.vel = Vector3D(RandFloat(-1.5f, 1.5f),
                         RandFloat(1.5f, 4.0f),
                         RandFloat(-1.5f, 1.5f));
        p.life = 0.0f;
        p.maxLife = BREAK_LIFETIME + RandFloat(-0.1f, 0.1f);
        p.obj = obj;
        m_breakParticles.push_back(p);
    }
}

void BlockEffects::Update(float deltaTime) {
    // Update place effects — animate scale via TransformComponent
    for (auto& effect : m_placeEffects) {
        effect.timer += deltaTime;
        if (effect.timer >= effect.duration) {
            if (effect.obj) {
                m_scene->RemoveObject(effect.obj);
                effect.obj = nullptr;
            }
            continue;
        }

        float t = effect.timer / effect.duration;
        // Ease-out for snappy feel
        t = 1.0f - (1.0f - t) * (1.0f - t);
        float scale = 0.3f + t * 0.7f;
        float offset = (1.0f - scale) * 0.5f;

        auto* tr = effect.obj->GetComponent<TransformComponent>();
        if (tr) {
            tr->SetPosition(Vector3D(effect.x + offset,
                                      effect.y + offset,
                                      effect.z + offset));
            tr->SetScale(Vector3D(scale, scale, scale));
        }
    }

    // Update break particles — physics via TransformComponent
    for (auto& p : m_breakParticles) {
        p.life += deltaTime;
        if (p.life >= p.maxLife) {
            if (p.obj) {
                m_scene->RemoveObject(p.obj);
                p.obj = nullptr;
            }
            continue;
        }

        p.vel = p.vel + Vector3D(0, PARTICLE_GRAVITY, 0) * deltaTime;
        p.pos = p.pos + p.vel * deltaTime;

        auto* tr = p.obj->GetComponent<TransformComponent>();
        if (tr) {
            tr->SetPosition(p.pos);
        }
    }

    // Clean up completed place effects (keep them for PopCompletedPlacements)
    // Clean up dead particles
    m_breakParticles.erase(
        std::remove_if(m_breakParticles.begin(), m_breakParticles.end(),
            [](const BreakParticle& p) { return p.obj == nullptr; }),
        m_breakParticles.end());
}

std::vector<BlockEffects::CompletedPlace> BlockEffects::PopCompletedPlacements() {
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
