#ifndef _HUD_PANEL_HPP_
#define _HUD_PANEL_HPP_

#include <Debug/SystemMetrics.hpp>

class ChunkManager;
class MainScene;

/// Top-left HUD readout (selected block, raycast look-at, position/direction,
/// FOV drag), the top-right performance panel, the crosshair, and the
/// save/load toast. Owns the metrics cache and its 0.5s refresh timer; camera
/// and app state are fetched fresh via the owning MainScene each call.
/// @ingroup ui
class HudPanel {
public:
    HudPanel(ChunkManager& chunkManager, MainScene& scene)
        : m_chunkManager(chunkManager), m_scene(scene) {}

    /// Refreshes the cached system metrics on a 0.5s cadence; call every
    /// frame regardless of UI visibility.
    void Update(float deltaTime);

    /// Draws the center crosshair, if enabled in settings.
    void RenderCrosshair();
    /// Draws the HUD readout, save/load toast, and performance panel.
    void Render();

private:
    /// Selected block, raycast look-at, position/direction, and FOV drag.
    void RenderHud();
    /// Renderer backend, cached system metrics, and culling stat readout.
    void RenderPerformance();
    /// Fades in/out the save/load feedback message near the top of the screen.
    void RenderToast();

    ChunkManager& m_chunkManager;
    MainScene& m_scene;

    Sleak::SystemMetricsData m_cachedMetrics;
    float m_metricTimer = 0.0f;
};

#endif
