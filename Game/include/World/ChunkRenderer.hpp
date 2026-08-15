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

    /// Distance/shadow flags, occluder submission, then frustum + occlusion.
    void UpdateVisibility();

    /// Draws visible opaque column meshes front-to-back.
    void RenderColumns();

    /// Draws visible water column meshes back-to-front, without shadows.
    void RenderWater();

    /// Forwards to CullingSystem frustum/occlusion toggles (used by UI).
    void SetCullingEnabled(bool frustum, bool occlusion);

private:
    ChunkManager& m_mgr;

    // Scratch buffers reused each frame (no per-frame allocation growth).
    std::vector<ColumnMesh*> m_cullCandidates;
    std::vector<ColumnMesh*> m_renderScratch;
    std::vector<ColumnMesh*> m_waterScratch;
};

#endif
