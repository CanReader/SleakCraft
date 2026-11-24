#ifndef _CHUNK_RENDERER_HPP_
#define _CHUNK_RENDERER_HPP_

#include "ChunkMesher.hpp"
#include <vector>

class ChunkManager;

/// Culls and submits a ChunkManager's column meshes to the render batches.
/// @ingroup world
class ChunkRenderer {
public:
    /// Binds the renderer to the manager owning the column map and materials.
    explicit ChunkRenderer(ChunkManager& mgr) : m_mgr(mgr) {}

    /// Distance/shadow flags, occluder submission, frustum + occlusion per
    /// column, then the frustum per chunk.
    void UpdateVisibility();

    /// Draws the visible chunks of each column front-to-back, without the
    /// directions facing away, plus whole columns as shadow casters.
    void RenderColumns();

    /// Draws the water of visible chunks back-to-front, without shadows.
    void RenderWater();

    /// Forwards to CullingSystem frustum/occlusion toggles (used by UI).
    void SetCullingEnabled(bool frustum, bool occlusion);

private:
    ChunkManager& m_mgr;

    // Scratch buffers reused each frame (no per-frame allocation growth).
    std::vector<ColumnMesh*> m_cullCandidates;
    std::vector<ColumnMesh*> m_renderScratch;
    std::vector<ColumnMesh*> m_waterScratch;
    std::vector<Sleak::MeshRange> m_cameraRanges;
    std::vector<Sleak::MeshRange> m_shadowRanges;
};

#endif
