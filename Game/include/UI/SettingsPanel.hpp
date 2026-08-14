#ifndef _SETTINGS_PANEL_HPP_
#define _SETTINGS_PANEL_HPP_

#include <Math/Vector.hpp>
#include <Runtime/Texture.hpp>

class ChunkManager;
class MainScene;
namespace Sleak { class DirectionalLight; }

// General/culling/lighting/SSAO/IBL/texture settings sections. Owns the
// live-edit sun/ambient/fog/texture state (crosshair/vsync/MT-loading/
// culling toggles stay on MainScene since MainScene::SetupLighting also
// reads them at Initialize time; they're read/written here through it).
class SettingsPanel {
public:
    SettingsPanel(ChunkManager& chunkManager, MainScene& scene)
        : m_chunkManager(chunkManager), m_scene(scene) {}

    void Render();

    // Hands the panel the live sun light so its edits apply immediately;
    // called once from MainScene::SetupLighting right after the light is
    // created.
    void AttachSun(Sleak::DirectionalLight* sun) { m_sun = sun; }

    // Initial-value accessors for MainScene::SetupMaterial/SetupLighting.
    Sleak::Math::Vector3D ComputeSunDirection() const;
    void GetSunColor(float& r, float& g, float& b) const {
        r = m_sunColorR;
        g = m_sunColorG;
        b = m_sunColorB;
    }
    float GetSunIntensity() const { return m_sunIntensity; }
    void GetAmbientColor(float& r, float& g, float& b) const {
        r = m_ambientColorR;
        g = m_ambientColorG;
        b = m_ambientColorB;
    }
    float GetAmbientIntensity() const { return m_ambientIntensity; }
    void GetFogHorizonColor(float& r, float& g, float& b) const {
        r = m_fogHorizonR;
        g = m_fogHorizonG;
        b = m_fogHorizonB;
    }
    void GetFogZenithColor(float& r, float& g, float& b) const {
        r = m_fogZenithR;
        g = m_fogZenithG;
        b = m_fogZenithB;
    }
    bool IsFogEnabled() const { return m_fogEnabled; }
    bool IsHeightFogEnabled() const { return m_heightFogEnabled; }
    float GetHeightFogTop() const { return m_heightFogTop; }
    float GetHeightFogDensity() const { return m_heightFogDensity; }
    float GetHeightFogFalloff() const { return m_heightFogFalloff; }
    Sleak::TextureFilter GetTextureFilter() const { return m_texFilter; }

private:
    void RenderGeneralSection();
    void RenderCullingSection();
    void RenderMSAASection();
    void RenderRenderDistanceSection();
    void RenderLightingSection();
    void RenderSSAOSection();
    void RenderIBLSection();
    void RenderTextureSection();

    ChunkManager& m_chunkManager;
    MainScene& m_scene;

    // Non-owning; the light itself is owned/created by MainScene.
    Sleak::DirectionalLight* m_sun = nullptr;

    // Sun state (live-editable)
    float m_sunElevation  = 65.0f;   // degrees above horizon (0=sunrise, 90=noon)
    float m_sunAzimuth    = 255.0f;  // degrees clockwise from north
    float m_sunIntensity  = 0.69f;
    float m_sunColorR     = 1.00f;
    float m_sunColorG     = 0.96f;
    float m_sunColorB     = 0.88f;

    // Ambient state
    float m_ambientIntensity = 0.725f;
    float m_ambientColorR    = 0.45f;
    float m_ambientColorG    = 0.62f;
    float m_ambientColorB    = 1.00f;

    // Texture quality state
    Sleak::TextureFilter m_texFilter = Sleak::TextureFilter::Nearest;
    float m_texLodBias = 0.0f;

    // Fog state (live-editable). Defaults align with LightManager defaults so
    // toggling the fog UI without touching sliders matches the engine's idle
    // state. Horizon RGB is overridden by SetupLighting() to a slightly
    // bluer tint that matches the skybox.
    bool  m_fogEnabled        = true;
    float m_fogHorizonR       = 0.62f;
    float m_fogHorizonG       = 0.78f;
    float m_fogHorizonB       = 1.00f;
    float m_fogZenithR        = 0.42f;
    float m_fogZenithG        = 0.58f;
    float m_fogZenithB        = 0.86f;
    bool  m_heightFogEnabled  = true;
    float m_heightFogTop      = 64.0f;
    float m_heightFogDensity  = 0.25f;
    float m_heightFogFalloff  = 0.05f;
};

#endif
