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
#include <cmath>

using namespace Sleak;
using namespace Sleak::Math;

static float RandFloat(float lo, float hi) {
    float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return lo + t * (hi - lo);
}

// Build a unit cube (0..1) with correct per-face textures
static void BuildBlockCube(BlockType type, VertexGroup& verts, IndexGroup& inds) {
    struct FaceInfo {
        float positions[4][3];
        float nx, ny, nz;
        BlockFace face;
    };

    FaceInfo faces[6] = {
        // Top
        {{{0,1,0}, {0,1,1}, {1,1,1}, {1,1,0}}, 0, 1, 0, BlockFace::Top},
        // Bottom
        {{{0,0,1}, {0,0,0}, {1,0,0}, {1,0,1}}, 0, -1, 0, BlockFace::Bottom},
        // North (+Z)
        {{{1,0,1}, {1,1,1}, {0,1,1}, {0,0,1}}, 0, 0, 1, BlockFace::North},
        // South (-Z)
        {{{0,0,0}, {0,1,0}, {1,1,0}, {1,0,0}}, 0, 0, -1, BlockFace::South},
        // East (+X)
        {{{1,0,0}, {1,1,0}, {1,1,1}, {1,0,1}}, 1, 0, 0, BlockFace::East},
        // West (-X)
        {{{0,0,1}, {0,1,1}, {0,1,0}, {0,0,0}}, -1, 0, 0, BlockFace::West},
    };

    for (int f = 0; f < 6; f++) {
        // Use the correct texture tile for each face
        AtlasUV uv = TextureAtlas::GetTileUV(
            GetBlockTextureTile(type, faces[f].face));

        float uvs[4][2] = {
            {uv.u0, uv.v1}, {uv.u0, uv.v0}, {uv.u1, uv.v0}, {uv.u1, uv.v1}
        };

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
    BuildBlockCube(type, verts, inds);

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

void BlockEffects::SpawnPlaceEffect(int x, int y, int z, BlockType type) {
    auto* obj = CreatePlaceCube(type);

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
        BreakParticle p;
        p.pos = Vector3D(cx + RandFloat(-0.3f, 0.3f),
                         cy + RandFloat(-0.3f, 0.3f),
                         cz + RandFloat(-0.3f, 0.3f));
        p.vel = Vector3D(RandFloat(-1.5f, 1.5f),
                         RandFloat(1.5f, 4.0f),
                         RandFloat(-1.5f, 1.5f));
        p.life = 0.0f;
        p.maxLife = BREAK_LIFETIME + RandFloat(-0.1f, 0.1f);
        p.tileIndex = tile;
        m_breakParticles.push_back(p);
    }
}

void BlockEffects::RebuildParticleMesh(const Vector3D& cameraPos) {
    // Remove old batched mesh
    if (m_particleMeshObj) {
        m_scene->RemoveObject(m_particleMeshObj);
        m_particleMeshObj = nullptr;
    }

    if (m_breakParticles.empty()) return;

    VertexGroup verts;
    IndexGroup inds;

    for (const auto& p : m_breakParticles) {
        AtlasUV uv = TextureAtlas::GetTileUV(p.tileIndex);

        // Billboard: compute right/up vectors facing camera
        Vector3D toCamera = cameraPos - p.pos;
        float dist = toCamera.Magnitude();
        if (dist < 0.001f) continue;
        toCamera = toCamera * (1.0f / dist); // normalize

        Vector3D worldUp(0, 1, 0);
        Vector3D right = worldUp.Cross(toCamera);
        float rightLen = right.Magnitude();
        if (rightLen < 0.001f) {
            right = Vector3D(1, 0, 0);
        } else {
            right = right * (1.0f / rightLen);
        }
        Vector3D up = toCamera.Cross(right);

        float half = PARTICLE_SIZE * 0.5f;
        // Fade out near end of life
        float alpha = 1.0f;
        if (p.life > p.maxLife * 0.7f)
            alpha = (p.maxLife - p.life) / (p.maxLife * 0.3f);

        Vector3D corners[4] = {
            p.pos - right * half - up * half,
            p.pos - right * half + up * half,
            p.pos + right * half + up * half,
            p.pos + right * half - up * half,
        };

        uint32_t base = static_cast<uint32_t>(verts.GetSize());
        float uvCoords[4][2] = {
            {uv.u0, uv.v1}, {uv.u0, uv.v0}, {uv.u1, uv.v0}, {uv.u1, uv.v1}
        };

        // Normal faces camera
        for (int i = 0; i < 4; i++) {
            Vertex v(corners[i].GetX(), corners[i].GetY(), corners[i].GetZ(),
                     toCamera.GetX(), toCamera.GetY(), toCamera.GetZ(),
                     1, 0, 0, 1,
                     uvCoords[i][0], uvCoords[i][1]);
            v.SetColor(1.0f, 1.0f, 1.0f, alpha);
            verts.AddVertex(v);
        }

        inds.add(base);     inds.add(base + 2); inds.add(base + 1);
        inds.add(base);     inds.add(base + 3); inds.add(base + 2);
    }

    if (verts.GetSize() == 0) return;

    MeshData meshData;
    meshData.vertices = std::move(verts);
    meshData.indices = std::move(inds);

    m_particleMeshObj = new GameObject("BreakParticles");
    m_particleMeshObj->AddComponent<TransformComponent>(Vector3D(0, 0, 0));
    m_particleMeshObj->AddComponent<MaterialComponent>(m_material);
    m_particleMeshObj->AddComponent<MeshComponent>(std::move(meshData));
    m_particleMeshObj->Initialize();
    m_scene->AddObject(m_particleMeshObj);
}

void BlockEffects::Update(float deltaTime, const Vector3D& cameraPos) {
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
        t = 1.0f - (1.0f - t) * (1.0f - t); // ease-out
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

    // Update break particles physics
    for (auto& p : m_breakParticles) {
        p.life += deltaTime;
        p.vel = p.vel + Vector3D(0, PARTICLE_GRAVITY, 0) * deltaTime;
        p.pos = p.pos + p.vel * deltaTime;
    }

    // Remove dead particles
    m_breakParticles.erase(
        std::remove_if(m_breakParticles.begin(), m_breakParticles.end(),
            [](const BreakParticle& p) { return p.life >= p.maxLife; }),
        m_breakParticles.end());

    // Rebuild single batched billboard mesh for all particles
    RebuildParticleMesh(cameraPos);
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
    m_placeEffects.clear();
    m_breakParticles.clear();
    if (m_particleMeshObj) {
        m_scene->RemoveObject(m_particleMeshObj);
        m_particleMeshObj = nullptr;
    }
}
