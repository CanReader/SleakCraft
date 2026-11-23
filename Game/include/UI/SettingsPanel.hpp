#ifndef _SETTINGS_PANEL_HPP_
#define _SETTINGS_PANEL_HPP_

#include <Runtime/Texture.hpp>

#include "World/VoxelLighting.hpp"

class ChunkManager;
class MainScene;
namespace Sleak {
class Application;
}

// TAA recovers sub-pixel texture detail, so the atlas can sample one mip sharper
#define TAA_TEXTURE_LOD_BIAS (-1.0f)

/// General/culling/sky/lighting/SSAO/IBL/texture settings sections. Owns the
/// live-edit palette multiplier and texture state; the world clock, the sky
/// options and the crosshair/vsync/MT-loading/culling toggles live on
/// MainScene and are read/written here through it.
/// @ingroup ui
class SettingsPanel {
public:
    SettingsPanel(ChunkManager& chunkManager, MainScene& scene);

    /// Draws every settings section in sequence.
    void Render();

    // Initial-value accessor for MainScene::SetupMaterial.
    Sleak::TextureFilter GetTextureFilter() const { return m_texFilter; }

    /// Applies the user LOD bias to the block atlas, plus the TAA bias while TAA is on.
    void ApplyTextureLodBias();

private:
    /// Crosshair, vsync, and multithreaded-loading toggles.
    void RenderGeneralSection();
    /// Frustum/occlusion toggles plus the live CullingSystem stat readout.
    void RenderCullingSection();
    /// MSAA sample-count dropdown, clamped to the renderer's supported max.
    void RenderMSAASection();
    /// Render distance slider; also re-derives fog distances on change.
    void RenderRenderDistanceSection();
    /// Time of day, pause and day length, then the fog and star options.
    void RenderSkySection();
    /// Voxel sun and ambient multipliers.
    void RenderLightingSection();
    /// SSAO toggle and radius/bias/power sliders.
    void RenderSSAOSection();
    /// IBL toggle and intensity slider.
    void RenderIBLSection();
    /// Texture filter and LOD bias, applied to the block material's texture.
    void RenderTextureSection();

    ChunkManager& m_chunkManager;
    Sleak::Application* m_app;
    MainScene& m_scene;

    bool isDeferredEnabled = false;
    bool isTAAEnabled = false;

    // Multipliers on the voxel palette's sun and sky ambient (1 = palette)
    LightMultiplier m_sunMultiplier;
    LightMultiplier m_ambientMultiplier;

    // Texture quality state
    Sleak::TextureFilter m_texFilter = Sleak::TextureFilter::Nearest;
    float m_texLodBias = 0.0f;
};

#endif
