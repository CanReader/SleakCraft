#include "World/ChunkRenderer.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Culling/CullingSystem.hpp>
#include <Runtime/Material.hpp>
#include <Runtime/MeshBatch.hpp>
#include <algorithm>

void ChunkRenderer::UpdateVisibility() {
    const auto& camPos = Sleak::Camera::GetMainCameraPosition();
    float camX = camPos.GetX();
    float camZ = camPos.GetZ();

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

        // Shadow-caster cull: only columns within the configured caster
        // distance render into the shadow map.
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
            continue;
        }

        col.visible = false;  // decided in Pass B
        m_cullCandidates.push_back(&col);
    }

    Sleak::CullingSystem::FinalizeOccluders();

    // Pass B: frustum + occlusion query for remaining candidates.
    for (ColumnMesh* col : m_cullCandidates)
        col->visible = Sleak::CullingSystem::IsVisible(col->bounds);
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
