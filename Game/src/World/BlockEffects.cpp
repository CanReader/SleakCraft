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

// Build a unit cube (0..1) with correct per-face textures
static void BuildBlockCube(BlockType type, VertexGroup& verts, IndexGroup& inds) {
    struct FaceInfo {
        float positions[4][3];
        float nx, ny, nz;
        BlockFace face;
    };

    FaceInfo faces[6] = {
        {{{0,1,0}, {0,1,1}, {1,1,1}, {1,1,0}}, 0, 1, 0, BlockFace::Top},
        {{{0,0,1}, {0,0,0}, {1,0,0}, {1,0,1}}, 0, -1, 0, BlockFace::Bottom},
        {{{1,0,1}, {1,1,1}, {0,1,1}, {0,0,1}}, 0, 0, 1, BlockFace::North},
        {{{0,0,0}, {0,1,0}, {1,1,0}, {1,0,0}}, 0, 0, -1, BlockFace::South},
        {{{1,0,0}, {1,1,0}, {1,1,1}, {1,0,1}}, 1, 0, 0, BlockFace::East},
        {{{0,0,1}, {0,1,1}, {0,1,0}, {0,0,0}}, -1, 0, 0, BlockFace::West},
    };

    for (int f = 0; f < 6; f++) {
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

GameObject* BlockEffects::CreateParticleQuad(uint8_t tileIndex) {
    VertexGroup verts;
    IndexGroup inds;

    AtlasUV uv = TextureAtlas::GetTileUV(tileIndex);

    // Two-sided quad on XY plane facing +Z, centered at origin
    // Rotated each frame to face camera via TransformComponent
    float h = 0.5f;
    uint32_t base = 0;
    Vertex v0(-h, -h, 0,  0, 0, 1,  1, 0, 0, 1,  uv.u0, uv.v1);
    Vertex v1(-h,  h, 0,  0, 0, 1,  1, 0, 0, 1,  uv.u0, uv.v0);
    Vertex v2( h,  h, 0,  0, 0, 1,  1, 0, 0, 1,  uv.u1, uv.v0);
    Vertex v3( h, -h, 0,  0, 0, 1,  1, 0, 0, 1,  uv.u1, uv.v1);
    v0.SetColor(1, 1, 1, 1); v1.SetColor(1, 1, 1, 1);
    v2.SetColor(1, 1, 1, 1); v3.SetColor(1, 1, 1, 1);
    verts.AddVertex(v0); verts.AddVertex(v1);
    verts.AddVertex(v2); verts.AddVertex(v3);

    // Front
    inds.add(base);     inds.add(base + 2); inds.add(base + 1);
    inds.add(base);     inds.add(base + 3); inds.add(base + 2);
    // Back (reverse winding)
    inds.add(base);     inds.add(base + 1); inds.add(base + 2);
    inds.add(base);     inds.add(base + 2); inds.add(base + 3);

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

        auto* obj = CreateParticleQuad(tile);
        auto* tr = obj->GetComponent<TransformComponent>();
        if (tr) tr->SetPosition(Vector3D(px, py, pz));
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

void BlockEffects::Update(float deltaTime, const Vector3D& cameraPos) {
    // Update place effects — animate scale
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

    // Update break particles — move via TransformComponent
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
            tr->LookAt(cameraPos);
        }
    }

    // Remove dead particles from list
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
