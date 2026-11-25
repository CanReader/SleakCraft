#include "World/ChunkRenderer.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Culling/CullingSystem.hpp>
#include <Runtime/Material.hpp>
#include <Runtime/MeshBatch.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Columns this near cast shadows even off screen, so terrain above
// caves/enclosed spaces stays in the shadow map.
constexpr float SHADOW_FORCE_DIST = 48.0f;

/// True unless the box lies wholly outside one clip plane of the light
/// view-projection (row vectors, depth range [0, 1], no depth clamp).
bool OverlapsLightClip(const Sleak::Math::AABB& box, const float* m) {
    constexpr float kMargin = 2.0f;
    const float lo[3] = {box.min.GetX() - kMargin, box.min.GetY() - kMargin,
                         box.min.GetZ() - kMargin};
    const float hi[3] = {box.max.GetX() + kMargin, box.max.GetY() + kMargin,
                         box.max.GetZ() + kMargin};
    bool left = true, right = true, bottom = true, top = true;
    bool nearOut = true, farOut = true;
    for (int i = 0; i < 8; ++i) {
        const float x = (i & 1) ? hi[0] : lo[0];
        const float y = (i & 2) ? hi[1] : lo[1];
        const float z = (i & 4) ? hi[2] : lo[2];
        const float cx = x * m[0] + y * m[4] + z * m[8] + m[12];
        const float cy = x * m[1] + y * m[5] + z * m[9] + m[13];
        const float cz = x * m[2] + y * m[6] + z * m[10] + m[14];
        const float cw = x * m[3] + y * m[7] + z * m[11] + m[15];
        left = left && cx < -cw;
        right = right && cx > cw;
        bottom = bottom && cy < -cw;
        top = top && cy > cw;
        nearOut = nearOut && cz < 0.0f;
        farOut = farOut && cz > cw;
    }
    return !(left || right || bottom || top || nearOut || farOut);
}

/// True when two range lists draw the same quads.
bool SameRanges(const std::vector<Sleak::MeshRange>& a,
                const std::vector<Sleak::MeshRange>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const Sleak::MeshRange& x, const Sleak::MeshRange& y) {
                          return x.firstIndex == y.firstIndex &&
                                 x.indexCount == y.indexCount &&
                                 x.baseVertex == y.baseVertex;
                      });
}

/// Draws camera and shadow ranges of one mesh, as one command when equal.
void SubmitRanges(const Sleak::MeshHandle& mesh,
                  const std::vector<Sleak::MeshRange>& camera,
                  const std::vector<Sleak::MeshRange>& shadow) {
    auto draw = [&mesh](const std::vector<Sleak::MeshRange>& ranges,
                        Sleak::MeshPasses passes) {
        Sleak::MeshBatch::DrawRanges(
            mesh, ranges.data(), static_cast<uint32_t>(ranges.size()), passes);
    };
    if (!camera.empty() && SameRanges(camera, shadow)) {
        draw(camera, Sleak::MeshPasses::All);
        return;
    }
    if (!camera.empty()) draw(camera, Sleak::MeshPasses::Camera);
    if (!shadow.empty()) draw(shadow, Sleak::MeshPasses::Shadow);
}

/// Block coordinate holding a world coordinate.
int BlockOf(float v) { return static_cast<int>(std::floor(v)); }

/// Chunk coordinate holding a world coordinate.
int ChunkOf(float v) {
    return static_cast<int>(std::floor(v / static_cast<float>(Chunk::SIZE)));
}

}  // namespace

void ChunkRenderer::UpdateGraph() {
    ++m_frame;
    const auto& cam = Sleak::Camera::GetMainCameraPosition();
    const std::array<int, 3> chunk = {ChunkOf(cam.GetX()), ChunkOf(cam.GetY()),
                                      ChunkOf(cam.GetZ())};
    // An eye in water sees through all water
    const bool water = IsBlockWater(m_mgr.GetBlockAt(
        BlockOf(cam.GetX()), BlockOf(cam.GetY()), BlockOf(cam.GetZ())));
    const bool due =
        m_graphDirty && m_frame - m_graphFrame >= GRAPH_REFRESH_FRAMES;
    if (chunk == m_graphChunk && water == m_graphWater && !m_graphUrgent &&
        !due)
        return;

    m_graph.Reset(chunk[0], chunk[2], m_mgr.GetRenderDistance());
    for (const ColumnMesh* col : m_mgr.GetLiveColumns()) {
        for (int k = 0; k < COLUMN_CHUNKS; ++k) {
            ChunkVisibilityGraph::Cell* cell = m_graph.At(
                col->key.x, col->key.yBand * BAND_SIZE + k, col->key.z);
            if (!cell) break;
            const ChunkVis& vis = col->chunks[k];
            *cell = {vis.seeThrough, vis.covered, vis.openWater};
        }
    }
    m_graph.Run(chunk[1], water, m_mgr.GetDrawDistSq());
    for (ColumnMesh* col : m_mgr.GetLiveColumns())
        col->reachableChunks = m_graph.Reached(col->key.x, col->key.z);

    // Buried casters, under the lowest surface of the column and its eight
    // neighbors; an unknown neighbor buries nothing
    const int radius = m_mgr.GetRenderDistance();
    const int width = 2 * radius + 1;
    m_surfaces.assign(static_cast<size_t>(width) * width, Chunk::NO_SURFACE);
    auto surfaceAt = [&](int cx, int cz) -> int* {
        const int lx = cx - chunk[0] + radius;
        const int lz = cz - chunk[2] + radius;
        if (lx < 0 || lx >= width || lz < 0 || lz >= width) return nullptr;
        return &m_surfaces[lx + lz * width];
    };
    for (const ColumnMesh* col : m_mgr.GetLiveColumns())
        if (int* surface = surfaceAt(col->key.x, col->key.z))
            *surface = col->minSurface;
    for (ColumnMesh* col : m_mgr.GetLiveColumns()) {
        int lowest = std::numeric_limits<int>::max();
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int* surface =
                    surfaceAt(col->key.x + dx, col->key.z + dz);
                lowest =
                    std::min(lowest, surface ? *surface : Chunk::NO_SURFACE);
            }
        col->buriedChunks = BuriedChunks(lowest);
    }

    m_graphChunk = chunk;
    m_graphWater = water;
    m_graphFrame = m_frame;
    m_graphDirty = false;
    m_graphUrgent = false;
}

void ChunkRenderer::UpdateVisibility() {
    UpdateGraph();

    const auto& camPos = Sleak::Camera::GetMainCameraPosition();
    float camX = camPos.GetX();
    float camZ = camPos.GetZ();
    const float* lightVP = m_mgr.GetShadowLightVP();
    auto inLightBox = [lightVP](const ColumnMesh& col) {
        return !lightVP || OverlapsLightClip(col.bounds, lightVP);
    };
    const bool wantOccluders = Sleak::CullingSystem::OccludersWanted();

    // Chunks the camera draws: reached by the graph, frustum against each
    // chunk's exact box
    const Sleak::ViewFrustum& frustum = Sleak::Camera::GetMainViewFrustum();
    const bool frustumOn = Sleak::CullingSystem::IsFrustumCullingEnabled();
    auto markChunks = [&](ColumnMesh& col) {
        const float x0 = col.bounds.min.GetX();
        const float z0 = col.bounds.min.GetZ();
        for (int k = 0; k < COLUMN_CHUNKS; ++k) {
            const ChunkVis& chunk = col.chunks[k];
            if (!chunk.hasQuads || !(col.reachableChunks >> k & 1)) continue;
            if (frustumOn &&
                !frustum.IsAABBVisible(
                    Sleak::Math::Vector3D(x0, chunk.minY, z0),
                    Sleak::Math::Vector3D(x0 + Chunk::SIZE, chunk.maxY,
                                          z0 + Chunk::SIZE)))
                continue;
            col.visibleChunks |= static_cast<uint8_t>(1u << k);
        }
    };

    // Pass A: distance cull, shadow flag, occluder submission.
    m_cullCandidates.clear();
    for (ColumnMesh* live : m_mgr.GetLiveColumns()) {
        ColumnMesh& col = *live;
        col.visible = false;
        col.castsShadow = false;
        col.visibleChunks = 0;
        if (!col.mesh.IsValid()) continue;

        float minX = col.bounds.min.GetX();
        float maxX = col.bounds.max.GetX();
        float minZ = col.bounds.min.GetZ();
        float maxZ = col.bounds.max.GetZ();

        // Horizontal-only distance check (XZ cylinder) so columns stay visible
        // when the player is high above the terrain
        float dx = (camX < minX) ? (minX - camX) : (camX > maxX) ? (camX - maxX) : 0.0f;
        float dz = (camZ < minZ) ? (minZ - camZ) : (camZ > maxZ) ? (camZ - maxZ) : 0.0f;
        col.distSq = dx * dx + dz * dz;
        if (col.distSq > m_mgr.GetDrawDistSq()) continue;

        // Shadow-caster cull: within caster distance, then (once visible)
        // inside the next shadow pass light box.
        const bool inCasterRange = col.distSq <= m_mgr.GetShadowCasterDistSq();

        // Near columns act as occluders — beyond half draw distance an
        // occluder hides almost nothing but still costs raster time.
        float occDist = m_mgr.GetDrawDistance() * 0.5f;
        if (wantOccluders && col.distSq <= occDist * occDist) {
            for (const auto& occ : col.occluders)
                Sleak::CullingSystem::SubmitOccluderBox(occ);
        }

        if (col.distSq <= SHADOW_FORCE_DIST * SHADOW_FORCE_DIST) {
            // Casts even off screen; skips the occlusion test
            col.visible = true;
            col.castsShadow = inCasterRange && inLightBox(col);
            markChunks(col);
            continue;
        }

        col.castsShadow = inCasterRange;  // final in Pass B
        m_cullCandidates.push_back(&col);
    }

    Sleak::CullingSystem::FinalizeOccluders();

    // Pass B: frustum + occlusion query for remaining candidates.
    for (ColumnMesh* col : m_cullCandidates) {
        col->visible = Sleak::CullingSystem::IsVisible(col->bounds);
        col->castsShadow = col->visible && col->castsShadow && inLightBox(*col);
        if (col->visible) markChunks(*col);
    }
}

void ChunkRenderer::SetCullingEnabled(bool frustum, bool occlusion) {
    Sleak::CullingSystem::SetFrustumCullingEnabled(frustum);
    Sleak::CullingSystem::SetOcclusionCullingEnabled(occlusion);
}

void ChunkRenderer::RenderColumns() {
    const auto& camPos = Sleak::Camera::GetMainCameraPosition();
    // Buried casters drop only while the eye is above ground; one band spans
    // the world
    const auto camColumn = m_mgr.GetColumns().find(
        {ChunkOf(camPos.GetX()), 0, ChunkOf(camPos.GetZ())});
    const bool aboveGround =
        camColumn != m_mgr.GetColumns().end() &&
        camColumn->second.minSurface != Chunk::NO_SURFACE &&
        camPos.GetY() >= camColumn->second.minSurface + BURIED_MARGIN;
    // Front-to-back draw order for better early-z / less overdraw.
    m_renderScratch.clear();
    for (ColumnMesh* col : m_mgr.GetLiveColumns())
        if (col->visibleChunks != 0 || col->castsShadow)
            m_renderScratch.push_back(col);
    std::sort(m_renderScratch.begin(), m_renderScratch.end(),
              [](const ColumnMesh* a, const ColumnMesh* b) {
                  return a->distSq < b->distSq;
              });

    Sleak::MeshBatch::BeginBatch(m_mgr.GetMaterial().get());
    for (ColumnMesh* col : m_renderScratch) {
        const StreamRanges& opaque =
            col->ranges[static_cast<int>(VoxelStream::Opaque)];

        // Camera: visible chunks, directions with a face toward the camera
        m_cameraRanges.clear();
        if (col->visibleChunks != 0) {
            uint8_t facing[COLUMN_CHUNKS];
            for (int k = 0; k < COLUMN_CHUNKS; ++k)
                facing[k] = FacingGroups(
                    camPos.GetX(), camPos.GetY(), camPos.GetZ(),
                    col->bounds.min.GetX(),
                    static_cast<float>((col->key.yBand * BAND_SIZE + k) *
                                       Chunk::SIZE),
                    col->bounds.min.GetZ());
            AppendStreamRanges(
                opaque,
                [&](int group, int slot) {
                    const int k = SlotChunk(group, slot);
                    return (col->visibleChunks >> k & 1) &&
                           (facing[k] >> group & 1);
                },
                m_cameraRanges);
        }

        // Shadows: every direction of the unburied chunks
        m_shadowRanges.clear();
        if (col->castsShadow) {
            const int buried = aboveGround ? col->buriedChunks : 0;
            AppendStreamRanges(
                opaque,
                [buried](int group, int slot) {
                    return SlotChunk(group, slot) >= buried;
                },
                m_shadowRanges);
        }

        SubmitRanges(col->mesh, m_cameraRanges, m_shadowRanges);
    }
    Sleak::MeshBatch::EndBatch();
}

void ChunkRenderer::RenderWater() {
    if (!m_mgr.GetWaterMaterial()) return;
    // Back-to-front draw order for correct transparency.
    m_waterScratch.clear();
    for (ColumnMesh* col : m_mgr.GetLiveColumns()) {
        const StreamRanges& water =
            col->ranges[static_cast<int>(VoxelStream::Water)];
        if (col->visibleChunks != 0 &&
            water.start[0] != water.start[VOXEL_DIR_GROUPS * COLUMN_CHUNKS])
            m_waterScratch.push_back(col);
    }
    std::sort(m_waterScratch.begin(), m_waterScratch.end(),
              [](const ColumnMesh* a, const ColumnMesh* b) {
                  return a->distSq > b->distSq;
              });

    // Water never casts shadows — keeps ~200 meshes out of the shadow map.
    Sleak::MeshBatch::BeginBatch(m_mgr.GetWaterMaterial().get());
    for (ColumnMesh* col : m_waterScratch) {
        // Two-sided: every direction of the visible chunks
        m_cameraRanges.clear();
        AppendStreamRanges(
            col->ranges[static_cast<int>(VoxelStream::Water)],
            [col](int group, int slot) {
                const int k = SlotChunk(group, slot);
                return (col->visibleChunks >> k & 1) != 0;
            },
            m_cameraRanges);
        if (!m_cameraRanges.empty())
            Sleak::MeshBatch::DrawRanges(
                col->mesh, m_cameraRanges.data(),
                static_cast<uint32_t>(m_cameraRanges.size()),
                Sleak::MeshPasses::Camera);
    }
    Sleak::MeshBatch::EndBatch();
}
