#include "UI/SettingsPanel.hpp"

#include "MainScene.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Core/Application.hpp>
#include <Culling/CullingSystem.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Lighting/DirectionalLight.hpp>
#include <Lighting/LightManager.hpp>
#include <Runtime/Material.hpp>
#include <UI/UI.hpp>
#include <cmath>

using namespace Sleak;
using namespace Sleak::Math;


  SettingsPanel::SettingsPanel(ChunkManager& chunkManager, MainScene& scene)
        : m_chunkManager(chunkManager), m_scene(scene) {
  m_app = Application::GetInstance();
  isDeferredEnabled = m_app->GetIsDeferredEnabled();
  isTAAEnabled = m_app->IsTAAEnabled();
}

Vector3D SettingsPanel::ComputeSunDirection() const {
    const float deg2rad = 0.01745329f;
    float eRad = m_sunElevation * deg2rad;
    float aRad = m_sunAzimuth * deg2rad;
    return Vector3D(-cosf(eRad) * sinf(aRad), -sinf(eRad),
                    -cosf(eRad) * cosf(aRad));
}

void SettingsPanel::Render() {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam || !m_app) return;

    auto* fpc = cam->GetComponent<FirstPersonController>();
    bool inGameMode = fpc && fpc->IsEnabled();

    int settingsFlags = UI::PanelFlags_NoTitleBar | UI::PanelFlags_AutoResize |
                        UI::PanelFlags_NoMove | UI::PanelFlags_NoFocusOnAppear;
    if (inGameMode) settingsFlags |= UI::PanelFlags_NoInput;
    UI::BeginPanel("Settings", 0, 180, 0.4f, settingsFlags);

    RenderGeneralSection();
    RenderCullingSection();
    RenderMSAASection();
    RenderRenderDistanceSection();
    RenderLightingSection();
    RenderSSAOSection();
    RenderIBLSection();
    RenderTextureSection();

    UI::EndPanel();
}

void SettingsPanel::RenderGeneralSection() {

    if (UI::Checkbox("Deferred Rendering", &isDeferredEnabled))
      m_app->SetDeferredEnabled(isDeferredEnabled);

    bool showCrosshair = m_scene.IsShowCrosshair();
    if (UI::Checkbox("Show Crosshair", &showCrosshair))
        m_scene.SetShowCrosshair(showCrosshair);

    bool vsync = m_scene.IsVSyncEnabled();
    if (UI::Checkbox("VSync", &vsync)) {
        m_scene.SetVSyncEnabled(vsync);
        if (m_app) m_app->SetVSync(vsync);
    }

    bool mtLoading = m_scene.IsMultithreadedLoading();
    if (UI::Checkbox("Multithreaded Loading", &mtLoading)) {
        m_scene.SetMultithreadedLoading(mtLoading);
        m_chunkManager.SetMultithreaded(mtLoading);
    }
}

void SettingsPanel::RenderCullingSection() {
    UI::Separator();
    UI::Text("-- Culling --");

    bool frustum = m_scene.IsFrustumCullingEnabled();
    bool occlusion = m_scene.IsOcclusionCullingEnabled();
    if (UI::Checkbox("Frustum Culling", &frustum)) {
        m_scene.SetFrustumCullingEnabled(frustum);
        m_chunkManager.SetCullingEnabled(frustum, occlusion);
    }
    if (UI::Checkbox("Occlusion Culling", &occlusion)) {
        m_scene.SetOcclusionCullingEnabled(occlusion);
        m_chunkManager.SetCullingEnabled(m_scene.IsFrustumCullingEnabled(),
                                         occlusion);
    }
    {
        const auto& cs = CullingSystem::GetStats();
        UI::Text("Tested %u  Frustum %u  Occ %u", cs.tested, cs.frustumCulled,
                 cs.occlusionCulled);
        if (cs.occlusionSkipped)
            UI::Text("Occlusion idle (adaptive)");
        else
            UI::Text("Occluders %u rast  %.2f ms", cs.occludersRasterized,
                     cs.rasterizeMs);
    }
}

void SettingsPanel::RenderMSAASection() {
    if (!m_app) return;

    UI::Separator();
    if(!m_app->GetIsDeferredEnabled())
    {
      UI::Text("Anti-Aliasing");
      {
        const char* labels[] = {"Off", "2x", "4x", "8x"};
        int values[] = {1, 2, 4, 8};
        int count = 1;
        uint32_t maxMSAA = m_app->GetMaxMSAASampleCount();
        for (int i = 1; i < 4; i++)
            if (static_cast<uint32_t>(values[i]) <= maxMSAA) count = i + 1;
        int current = 0;
        for (int i = 0; i < count; i++)
            if (static_cast<uint32_t>(values[i]) == m_app->GetMSAASampleCount())
                current = i;
        if (UI::Combo("MSAA", &current, labels, count))
            m_app->SetMSAASampleCount(values[current]);
      }
    } else {
      isTAAEnabled = m_app->IsTAAEnabled();
      if (UI::Checkbox("Temporal Anti-Aliasing", &isTAAEnabled)) {
          m_app->SetTAAEnabled(isTAAEnabled);
          ApplyTextureLodBias();
      }
      if (isTAAEnabled) {
          float sharpen = m_app->GetSharpenStrength();
          if (UI::DragFloat("Sharpen", &sharpen, 0.01f, 0.0f, 1.0f))
              m_app->SetSharpenStrength(sharpen);
      }
  }
}

void SettingsPanel::RenderRenderDistanceSection() {
    UI::Separator();
    float rd = static_cast<float>(m_chunkManager.GetRenderDistance());
    if (UI::DragFloat("Render Distance", &rd, 1.0f, 2.0f, 16.0f)) {
        m_chunkManager.SetRenderDistance(static_cast<int>(rd));
        float drawDist = m_chunkManager.GetDrawDistance();
        auto* lm = m_scene.GetLightManager();
        if (lm) lm->SetFogDistances(drawDist * 0.9f, drawDist);
        // Shadow frustum stays fixed — not tied to draw distance
        // (scaling it causes low-res shadows and disappearing issues)
    }
}

void SettingsPanel::RenderLightingSection() {
    UI::Separator();
    UI::Text("-- Sun --");

    bool sunDirChanged = false;
    sunDirChanged |=
        UI::DragFloat("Elevation", &m_sunElevation, 0.5f, -10.0f, 90.0f);
    sunDirChanged |=
        UI::DragFloat("Azimuth", &m_sunAzimuth, 1.0f, 0.0f, 360.0f);
    if (sunDirChanged && m_sun) m_sun->SetDirection(ComputeSunDirection());

    if (UI::DragFloat("Sun Intensity", &m_sunIntensity, 0.01f, 0.0f, 5.0f) &&
        m_sun)
        m_sun->SetIntensity(m_sunIntensity);

    bool sunColorChanged = false;
    sunColorChanged |= UI::DragFloat("Sun R", &m_sunColorR, 0.005f, 0.0f, 1.0f);
    sunColorChanged |= UI::DragFloat("Sun G", &m_sunColorG, 0.005f, 0.0f, 1.0f);
    sunColorChanged |= UI::DragFloat("Sun B", &m_sunColorB, 0.005f, 0.0f, 1.0f);
    if (sunColorChanged && m_sun)
        m_sun->SetColor(m_sunColorR, m_sunColorG, m_sunColorB);

    UI::Separator();
    UI::Text("-- Ambient --");

    auto* lm = m_scene.GetLightManager();
    if (UI::DragFloat("Amb Intensity", &m_ambientIntensity, 0.005f, 0.0f,
                      2.0f) &&
        lm)
        lm->SetAmbientIntensity(m_ambientIntensity);

    bool ambColorChanged = false;
    ambColorChanged |=
        UI::DragFloat("Amb R", &m_ambientColorR, 0.005f, 0.0f, 1.0f);
    ambColorChanged |=
        UI::DragFloat("Amb G", &m_ambientColorG, 0.005f, 0.0f, 1.0f);
    ambColorChanged |=
        UI::DragFloat("Amb B", &m_ambientColorB, 0.005f, 0.0f, 1.0f);
    if (ambColorChanged && lm)
        lm->SetAmbientColor(m_ambientColorR, m_ambientColorG, m_ambientColorB);

    UI::Separator();
    UI::Text("-- Fog --");

    if (UI::Checkbox("Fog Enabled", &m_fogEnabled) && lm)
        lm->SetFogEnabled(m_fogEnabled);

    bool horizonChanged = false;
    horizonChanged |=
        UI::DragFloat("Horizon R", &m_fogHorizonR, 0.005f, 0.0f, 1.0f);
    horizonChanged |=
        UI::DragFloat("Horizon G", &m_fogHorizonG, 0.005f, 0.0f, 1.0f);
    horizonChanged |=
        UI::DragFloat("Horizon B", &m_fogHorizonB, 0.005f, 0.0f, 1.0f);
    if (horizonChanged && lm)
        lm->SetFogColor(m_fogHorizonR, m_fogHorizonG, m_fogHorizonB);

    bool zenithChanged = false;
    zenithChanged |=
        UI::DragFloat("Zenith R", &m_fogZenithR, 0.005f, 0.0f, 1.0f);
    zenithChanged |=
        UI::DragFloat("Zenith G", &m_fogZenithG, 0.005f, 0.0f, 1.0f);
    zenithChanged |=
        UI::DragFloat("Zenith B", &m_fogZenithB, 0.005f, 0.0f, 1.0f);
    if (zenithChanged && lm)
        lm->SetFogZenithColor(m_fogZenithR, m_fogZenithG, m_fogZenithB);

    if (UI::Checkbox("Height Fog", &m_heightFogEnabled) && lm)
        lm->SetHeightFogEnabled(m_heightFogEnabled);

    if (UI::DragFloat("Height Top", &m_heightFogTop, 0.5f, -64.0f, 256.0f) &&
        lm)
        lm->SetHeightFogTop(m_heightFogTop);
    if (UI::DragFloat("Height Density", &m_heightFogDensity, 0.005f, 0.0f,
                      1.0f) &&
        lm)
        lm->SetHeightFogDensity(m_heightFogDensity);
    if (UI::DragFloat("Height Falloff", &m_heightFogFalloff, 0.001f, 0.0f,
                      1.0f) &&
        lm)
        lm->SetHeightFogFalloff(m_heightFogFalloff);
}

void SettingsPanel::RenderSSAOSection() {
    UI::Separator();
    UI::Text("-- SSAO --");
    if (m_app) {
        bool ssaoEnabled = m_app->IsSSAOEnabled();
        float ssaoRadius = m_app->GetSSAORadius();
        float ssaoBias = m_app->GetSSAOBias();
        float ssaoPower = m_app->GetSSAOPower();
        if (UI::Checkbox("SSAO Enabled", &ssaoEnabled))
            m_app->SetSSAOEnabled(ssaoEnabled);
        if (ssaoEnabled) {
            if (UI::DragFloat("SSAO Radius", &ssaoRadius, 0.01f, 0.05f, 4.0f))
                m_app->SetSSAORadius(ssaoRadius);
            if (UI::DragFloat("SSAO Bias", &ssaoBias, 0.001f, 0.0f, 0.2f))
                m_app->SetSSAOBias(ssaoBias);
            if (UI::DragFloat("SSAO Power", &ssaoPower, 0.05f, 0.1f, 8.0f))
                m_app->SetSSAOPower(ssaoPower);
        }
    }
}

void SettingsPanel::RenderIBLSection() {
    UI::Separator();
    UI::Text("-- IBL --");
        bool iblEnabled = m_app->IsIBLEnabled();
        float iblIntensity = m_app->GetIBLIntensity();
        if (UI::Checkbox("IBL Enabled", &iblEnabled))
            m_app->SetIBLEnabled(iblEnabled);
        if (iblEnabled) {
            if (UI::DragFloat("IBL Intensity", &iblIntensity, 0.05f, 0.0f,
                              5.0f))
                m_app->SetIBLIntensity(iblIntensity);
        }
}

void SettingsPanel::RenderTextureSection() {
    UI::Separator();
    UI::Text("-- Texture --");

    auto blockMaterial = m_scene.GetBlockMaterial();

    {
        const char* filterLabels[] = {"Nearest",  "Bilinear", "Trilinear",
                                      "Aniso 2x", "Aniso 4x", "Aniso 8x",
                                      "Aniso 16x"};
        const TextureFilter filterValues[] = {
            TextureFilter::Nearest,       TextureFilter::Bilinear,
            TextureFilter::Trilinear,     TextureFilter::Anisotropic2x,
            TextureFilter::Anisotropic4x, TextureFilter::Anisotropic8x,
            TextureFilter::Anisotropic16x};
        constexpr int filterCount = 7;
        int currentFilter = 0;
        for (int i = 0; i < filterCount; i++)
            if (filterValues[i] == m_texFilter) {
                currentFilter = i;
                break;
            }

        if (UI::Combo("Filter", &currentFilter, filterLabels, filterCount)) {
            m_texFilter = filterValues[currentFilter];
            auto* tex =
                blockMaterial ? blockMaterial->GetDiffuseTexture() : nullptr;
            if (tex) tex->SetFilter(m_texFilter);
        }
    }

    if (UI::DragFloat("LOD Bias", &m_texLodBias, 0.05f, -4.0f, 4.0f))
        ApplyTextureLodBias();
}

void SettingsPanel::ApplyTextureLodBias() {
    auto blockMaterial = m_scene.GetBlockMaterial();
    auto* tex = blockMaterial ? blockMaterial->GetDiffuseTexture() : nullptr;
    if (!tex) return;
    const bool taa = m_app && m_app->IsTAAEnabled();
    tex->SetLodBias(m_texLodBias + (taa ? TAA_TEXTURE_LOD_BIAS : 0.0f));
}
