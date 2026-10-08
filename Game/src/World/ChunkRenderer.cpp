#include "World/ChunkRenderer.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Culling/CullingSystem.hpp>
#include <Runtime/Material.hpp>
#include <Runtime/MeshBatch.hpp>
#include <algorithm>

namespace {

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

}  // namespace

void ChunkRenderer::UpdateVisibility() {
    const auto& camPos = Sleak::Camera::GetMainCameraPosition();
    float camX = camPos.GetX();
    float camZ = camPos.GetZ();
    const float* lightVP = m_mgr.GetShadowLightVP();

    // Force-render columns near the player regardless of camera frustum, so
    // terrain above caves/enclosed spaces stays in the shadow map.
    constexpr float SHADOW_FORCE_DIST = 48.0f;

    // Pass A: distance cull, shadow flag, occluder submission.
    m_cullCandidates.clear();
    for (auto& [key, col] : m_mgr.GetColumns()) {
        if (!col.mesh.IsValid() && !col.waterMesh.IsValid()) {
            col.visible = false;
            continue;
        }

        float minX = col.bounds.min.GetX();
        float maxX = col.bounds.max.GetX();
        float minZ = col.bounds.min.GetZ();
        float maxZ = col.bounds.max.GetZ();

        // Horizontal-only distance check (XZ cylinder) so columns stay visible
        // when the player is high above the terrain
        float dx = (camX < minX) ? (minX - camX) : (camX > maxX) ? (camX - maxX) : 0.0f;
        float dz = (camZ < minZ) ? (minZ - camZ) : (camZ > maxZ) ? (camZ - maxZ) : 0.0f;
        col.distSq = dx * dx + dz * dz;

        // Shadow-caster cull: within caster distance, then (once visible)
        // inside the next shadow pass light box.
        col.castsShadow = (col.distSq <= m_mgr.GetShadowCasterDistSq());

        if (col.distSq > m_mgr.GetDrawDistSq()) {
            col.visible = false;
            continue;
        }

        // Near columns act as occluders — beyond half draw distance an
        // occluder hides almost nothing but still costs raster time.
        float occDist = m_mgr.GetDrawDistance() * 0.5f;
        if (col.distSq <= occDist * occDist) {
            for (const auto& occ : col.occluders)
                Sleak::CullingSystem::SubmitOccluderBox(occ);
        }

        if (col.distSq <= SHADOW_FORCE_DIST * SHADOW_FORCE_DIST) {
            col.visible = true;  // force-visible, skip occlusion test
            if (col.castsShadow && lightVP)
                col.castsShadow = OverlapsLightClip(col.bounds, lightVP);
            continue;
        }

        col.visible = false;  // decided in Pass B
        m_cullCandidates.push_back(&col);
    }

    Sleak::CullingSystem::FinalizeOccluders();

    // Pass B: frustum + occlusion query for remaining candidates.
    for (ColumnMesh* col : m_cullCandidates) {
        col->visible = Sleak::CullingSystem::IsVisible(col->bounds);
        if (col->visible && col->castsShadow && lightVP)
            col->castsShadow = OverlapsLightClip(col->bounds, lightVP);
    }
}

void ChunkRenderer::SetCullingEnabled(bool frustum, bool occlusion) {
    Sleak::CullingSystem::SetFrustumCullingEnabled(frustum);
    Sleak::CullingSystem::SetOcclusionCullingEnabled(occlusion);
}

void ChunkRenderer::RenderColumns() {
    // Front-to-back draw order for better early-z / less overdraw.
    m_renderScratch.clear();
    for (auto& [key, col] : m_mgr.GetColumns())
        if (col.visible && col.mesh.IsValid())
            m_renderScratch.push_back(&col);
    std::sort(m_renderScratch.begin(), m_renderScratch.end(),
              [](const ColumnMesh* a, const ColumnMesh* b) {
                  return a->distSq < b->distSq;
              });

    Sleak::MeshBatch::BeginBatch(m_mgr.GetMaterial().get());
    for (ColumnMesh* col : m_renderScratch)
        Sleak::MeshBatch::Draw(col->mesh, col->castsShadow);
    Sleak::MeshBatch::EndBatch();
}

void ChunkRenderer::RenderWater() {
    if (!m_mgr.GetWaterMaterial()) return;
    // Back-to-front draw order for correct transparency.
    m_waterScratch.clear();
    for (auto& [key, col] : m_mgr.GetColumns())
        if (col.visible && col.waterMesh.IsValid())
            m_waterScratch.push_back(&col);
    std::sort(m_waterScratch.begin(), m_waterScratch.end(),
              [](const ColumnMesh* a, const ColumnMesh* b) {
                  return a->distSq > b->distSq;
              });

    // Water never casts shadows — keeps ~200 meshes out of the shadow map.
    Sleak::MeshBatch::BeginBatch(m_mgr.GetWaterMaterial().get());
    for (ColumnMesh* col : m_waterScratch)
        Sleak::MeshBatch::Draw(col->waterMesh, false);
    Sleak::MeshBatch::EndBatch();
}
