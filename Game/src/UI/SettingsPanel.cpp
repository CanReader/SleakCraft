#include "UI/SettingsPanel.hpp"

#include "MainScene.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Core/Application.hpp>
#include <Culling/CullingSystem.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Lighting/LightManager.hpp>
#include <Runtime/Material.hpp>
#include <UI/UI.hpp>

using namespace Sleak;

namespace {

// Voxel palette multiplier sliders
constexpr float MULTIPLIER_CHANNEL_STEP = 0.005f;
constexpr float MULTIPLIER_CHANNEL_MAX = 2.0f;
constexpr float MULTIPLIER_SCALE_STEP = 0.01f;
constexpr float MULTIPLIER_SCALE_MAX = 4.0f;
// Time and sky sliders
constexpr float TIME_STEP_HOURS = 0.05f;
constexpr float DAY_LENGTH_STEP_SECONDS = 15.0f;
constexpr float FOG_DENSITY_STEP = 0.01f;
constexpr float FOG_DENSITY_MAX = 4.0f;
constexpr float CLOUD_COVERAGE_STEP = 0.01f;

/// Intensity and RGB sliders of one multiplier; true when any changed.
bool DragMultiplier(const char* intensity, const char* r, const char* g,
                    const char* b, LightMultiplier& m) {
    bool changed = UI::DragFloat(intensity, &m.scale, MULTIPLIER_SCALE_STEP,
                                 0.0f, MULTIPLIER_SCALE_MAX);
    changed |= UI::DragFloat(r, &m.r, MULTIPLIER_CHANNEL_STEP, 0.0f,
                             MULTIPLIER_CHANNEL_MAX);
    changed |= UI::DragFloat(g, &m.g, MULTIPLIER_CHANNEL_STEP, 0.0f,
                             MULTIPLIER_CHANNEL_MAX);
    changed |= UI::DragFloat(b, &m.b, MULTIPLIER_CHANNEL_STEP, 0.0f,
                             MULTIPLIER_CHANNEL_MAX);
    return changed;
}

}  // namespace

  SettingsPanel::SettingsPanel(ChunkManager& chunkManager, MainScene& scene)
        : m_chunkManager(chunkManager), m_scene(scene) {
  m_app = Application::GetInstance();
  isDeferredEnabled = m_app->GetIsDeferredEnabled();
  isTAAEnabled = m_app->IsTAAEnabled();
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
    RenderSkySection();
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

void SettingsPanel::RenderSkySection() {
    UI::Separator();
    UI::Text("-- Sky --");

    WorldClock& clock = m_scene.GetWorldClock();
    float hours = static_cast<float>(
        WorldClock::DayFractionToHours(clock.GetDayFraction()));
    if (UI::DragFloat("Time of day (h)", &hours, TIME_STEP_HOURS, 0.0f,
                      HOURS_PER_DAY))
        clock.SetDayFraction(WorldClock::HoursToDayFraction(hours));
    bool paused = clock.IsPaused();
    if (UI::Checkbox("Pause time", &paused)) clock.SetPaused(paused);
    float dayLength = clock.GetDayLength();
    if (UI::DragFloat("Day length (s)", &dayLength, DAY_LENGTH_STEP_SECONDS,
                      MIN_DAY_LENGTH_SECONDS, MAX_DAY_LENGTH_SECONDS))
        clock.SetDayLength(dayLength);

    SkyOptions& sky = m_scene.GetSkyOptions();
    UI::Checkbox("Fog", &sky.fogEnabled);
    UI::DragFloat("Fog density", &sky.fogDensity, FOG_DENSITY_STEP, 0.0f,
                  FOG_DENSITY_MAX);
    UI::Checkbox("Height fog", &sky.heightFogEnabled);
    UI::Checkbox("Stars", &sky.starsEnabled);
    UI::DragFloat("Clouds", &sky.cloudCoverage, CLOUD_COVERAGE_STEP, 0.0f,
                  1.0f);
}

void SettingsPanel::RenderLightingSection() {
    UI::Separator();
    UI::Text("-- Sun --");

    bool paletteChanged = DragMultiplier("Sun Intensity", "Sun R", "Sun G",
                                         "Sun B", m_sunMultiplier);

    UI::Separator();
    UI::Text("-- Ambient --");

    paletteChanged |= DragMultiplier("Amb Intensity", "Amb R", "Amb G", "Amb B",
                                     m_ambientMultiplier);
    if (paletteChanged)
        m_scene.GetVoxelLighting().SetMultipliers(m_sunMultiplier,
                                                  m_ambientMultiplier);
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
