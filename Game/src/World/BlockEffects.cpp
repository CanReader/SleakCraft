#include "World/BlockEffects.hpp"

#include <Runtime/Material.hpp>
#include <algorithm>
#include <cstdlib>

#include "World/TextureAtlas.hpp"

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
// A particle quad is drawn from both sides
constexpr int PARTICLE_SIDES = 2;
constexpr float HALF = 0.5f;
// Billboard fallbacks when the camera sits on the particle or above it
constexpr float BILLBOARD_EPSILON = 1e-4f;
const Vector3D WORLD_UP(0.0f, 1.0f, 0.0f);
const Vector3D WORLD_FORWARD(0.0f, 0.0f, 1.0f);
const Vector3D WORLD_RIGHT(1.0f, 0.0f, 0.0f);

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

}  // namespace

void BlockEffects::Initialize(const RefPtr<Material>& material) {
    m_material = material;
}

void BlockEffects::SpawnPlaceEffect(int x, int y, int z, BlockType type) {
    PlaceEffect effect;
    effect.x = x;
    effect.y = y;
    effect.z = z;
    effect.type = type;
    effect.timer = 0.0f;
    effect.duration = PLACE_DURATION;
    m_placeEffects.push_back(effect);
}

void BlockEffects::SpawnBreakEffect(int x, int y, int z, BlockType type) {
    const uint8_t tile = GetBlockTextureTile(type, BlockFace::South);
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
        p.tile = tile;
        m_breakParticles.push_back(p);
    }
}

void BlockEffects::Update(float deltaTime, const Vector3D& cameraPos) {
    for (auto& effect : m_placeEffects) {
        effect.timer += deltaTime;
        if (effect.timer >= effect.duration) effect.done = true;
    }

    for (auto& p : m_breakParticles) {
        p.life += deltaTime;
        p.vel = p.vel + Vector3D(0, PARTICLE_GRAVITY, 0) * deltaTime;
        p.pos = p.pos + p.vel * deltaTime;
    }
    m_breakParticles.erase(
        std::remove_if(
            m_breakParticles.begin(), m_breakParticles.end(),
            [](const BreakParticle& p) { return p.life >= p.maxLife; }),
        m_breakParticles.end());

    RebuildMesh(cameraPos);
}

void BlockEffects::Render() const {
    if (!m_mesh.IsValid() || !m_material.IsValid()) return;
    MeshBatch::BeginBatch(m_material.get());
    MeshBatch::Draw(m_mesh);
    MeshBatch::EndBatch();
}

void BlockEffects::AppendPlaceCube(const PlaceEffect& effect) {
    // Ease-out scale-in, centered on the block
    float t = effect.timer / effect.duration;
    t = 1.0f - (1.0f - t) * (1.0f - t);
    const float scale = PLACE_START_SCALE + t * (1.0f - PLACE_START_SCALE);
    const float offset = (1.0f - scale) * HALF;
    const float origin[3] = {effect.x + offset, effect.y + offset,
                             effect.z + offset};

    for (const FaceInfo& f : CUBE_FACES) {
        float uvs[QUAD_CORNERS][2];
        QuadUVs(
            TextureAtlas::GetTileUV(GetBlockTextureTile(effect.type, f.face)),
            uvs);
        const uint32_t base = static_cast<uint32_t>(m_vertices.GetSize());
        for (int i = 0; i < QUAD_CORNERS; i++) {
            m_vertices.AddVertex(
                VoxelVertex(origin[0] + f.positions[i][0] * scale,
                            origin[1] + f.positions[i][1] * scale,
                            origin[2] + f.positions[i][2] * scale, f.nx, f.ny,
                            f.nz, uvs[i][0], uvs[i][1]));
        }
        AppendIndices(m_indices, base, QUAD_FRONT);
    }
}

void BlockEffects::AppendParticle(const BreakParticle& particle,
                                  const Vector3D& cameraPos) {
    // Billboard basis: forward to the camera, right and up across it
    Vector3D forward = cameraPos - particle.pos;
    forward = forward.Magnitude() > BILLBOARD_EPSILON ? forward.Normalized()
                                                      : WORLD_FORWARD;
    Vector3D right = WORLD_UP.Cross(forward);
    right = right.Magnitude() > BILLBOARD_EPSILON ? right.Normalized()
                                                  : WORLD_RIGHT;
    const Vector3D up = forward.Cross(right);

    const float h = PARTICLE_SIZE * HALF;
    // Bottom right, top right, top left, bottom left, as a north face
    const Vector3D corners[QUAD_CORNERS] = {
        particle.pos + right * h - up * h, particle.pos + right * h + up * h,
        particle.pos - right * h + up * h, particle.pos - right * h - up * h};
    float uvs[QUAD_CORNERS][2];
    QuadUVs(TextureAtlas::GetTileUV(particle.tile), uvs);

    for (int side = 0; side < PARTICLE_SIDES; side++) {
        const Vector3D n = side == 0 ? forward : forward * -1.0f;
        const uint32_t base = static_cast<uint32_t>(m_vertices.GetSize());
        for (int i = 0; i < QUAD_CORNERS; i++) {
            m_vertices.AddVertex(VoxelVertex(
                corners[i].GetX(), corners[i].GetY(), corners[i].GetZ(),
                n.GetX(), n.GetY(), n.GetZ(), uvs[i][0], uvs[i][1]));
        }
        AppendIndices(m_indices, base, side == 0 ? QUAD_FRONT : QUAD_BACK);
    }
}

void BlockEffects::RebuildMesh(const Vector3D& cameraPos) {
    m_vertices.clear();
    m_indices.clear();
    for (const auto& effect : m_placeEffects)
        if (!effect.done) AppendPlaceCube(effect);
    for (const auto& p : m_breakParticles) AppendParticle(p, cameraPos);

    if (m_indices.empty()) {
        m_mesh = MeshHandle();
        return;
    }
    m_mesh = MeshBatch::CreateMesh(GetVoxelVertexFormat(), m_vertices.GetData(),
                                   m_vertices.GetSizeInBytes(),
                                   m_indices.data(), m_indices.size());
}

std::vector<BlockEffects::CompletedPlace>
BlockEffects::PopCompletedPlacements() {
    std::vector<CompletedPlace> result;
    for (auto it = m_placeEffects.begin(); it != m_placeEffects.end();) {
        if (it->done) {
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
    for (auto& e : m_placeEffects) result.push_back({e.x, e.y, e.z, e.type});
    m_placeEffects.clear();
    return result;
}

void BlockEffects::Cleanup() {
    m_placeEffects.clear();
    m_breakParticles.clear();
    m_mesh = MeshHandle();
}
