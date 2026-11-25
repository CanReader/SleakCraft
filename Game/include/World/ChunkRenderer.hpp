#ifndef _CHUNK_RENDERER_HPP_
#define _CHUNK_RENDERER_HPP_

#include "ChunkMesher.hpp"
#include "ChunkVisibilityGraph.hpp"
#include <array>
#include <climits>
#include <vector>

class ChunkManager;

/// Culls and submits a ChunkManager's column meshes to the render batches.
/// @ingroup world
class ChunkRenderer {
public:
    /// Binds the renderer to the manager owning the column map and materials.
    explicit ChunkRenderer(ChunkManager& mgr) : m_mgr(mgr) {}

    /// Visibility graph, distance/shadow flags, occluder submission,
    /// frustum + occlusion per column, then the frustum per chunk.
    void UpdateVisibility();

    /// Draws the visible chunks of each column front-to-back, without the
    /// directions facing away, plus whole columns as shadow casters.
    void RenderColumns();

    /// Draws the water of visible chunks back-to-front, without shadows.
    void RenderWater();

    /// Forwards to CullingSystem frustum/occlusion toggles (used by UI).
    void SetCullingEnabled(bool frustum, bool occlusion);

    /// Columns changed: urgent re-runs the graph next frame, otherwise it
    /// re-runs within GRAPH_REFRESH_FRAMES.
    void MarkGraphDirty(bool urgent) {
        m_graphDirty = true;
        m_graphUrgent = m_graphUrgent || urgent;
    }

private:
    /// Frames between graph runs for streaming changes.
    static constexpr uint32_t GRAPH_REFRESH_FRAMES = 8;

    /// Re-runs the visibility graph when the camera changed chunk or medium,
    /// after an edit, or when streaming changes are due.
    void UpdateGraph();

    ChunkManager& m_mgr;

    ChunkVisibilityGraph m_graph;
    bool m_graphDirty = true;
    bool m_graphUrgent = true;
    uint32_t m_frame = 0;
    uint32_t m_graphFrame = 0;
    std::array<int, 3> m_graphChunk = {INT_MIN, INT_MIN, INT_MIN};
    bool m_graphWater = false;

    // Scratch buffers reused each frame (no per-frame allocation growth).
    std::vector<ColumnMesh*> m_cullCandidates;
    std::vector<ColumnMesh*> m_renderScratch;
    std::vector<ColumnMesh*> m_waterScratch;
    std::vector<Sleak::MeshRange> m_cameraRanges;
    std::vector<Sleak::MeshRange> m_shadowRanges;
    std::vector<int> m_surfaces;
};

#endif
