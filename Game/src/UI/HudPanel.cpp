#include "UI/HudPanel.hpp"

#include "MainScene.hpp"
#include "World/Block.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Core/Application.hpp>
#include <Runtime/Material.hpp>
#include <UI/UI.hpp>

using namespace Sleak;

void HudPanel::Update(float deltaTime) {
    m_metricTimer += deltaTime;
    if (m_metricTimer >= 0.5f) {
        m_cachedMetrics = SystemMetrics::Query();
        m_metricTimer = 0.0f;
    }
}

void HudPanel::RenderCrosshair() {
    if (!m_scene.IsShowCrosshair()) return;

    float cx = UI::GetViewportWidth() * 0.5f;
    float cy = UI::GetViewportHeight() * 0.5f;
    constexpr float arm = 10.0f;
    UI::DrawLine(cx - arm, cy, cx + arm, cy, 1.0f, 1.0f, 1.0f, 0.8f, 2.0f);
    UI::DrawLine(cx, cy - arm, cx, cy + arm, 1.0f, 1.0f, 1.0f, 0.8f, 2.0f);
}

void HudPanel::Render() {
    RenderHud();
    RenderToast();
    RenderPerformance();
}

void HudPanel::RenderHud() {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;

    UI::BeginPanel("HUD", 0, 0, 0.4f,
                   UI::PanelFlags_NoTitleBar | UI::PanelFlags_AutoResize |
                       UI::PanelFlags_NoMove | UI::PanelFlags_NoFocusOnAppear);

    UI::Text("Selected: %s [%d]", GetBlockName(m_scene.GetSelectedBlock()),
             static_cast<int>(m_scene.GetSelectedBlock()));

    auto dir = cam->GetDirection();
    auto rayHit = m_chunkManager.VoxelRaycast(cam->GetPosition(), dir, 6.0f);
    if (rayHit.hit) {
        UI::Text("Looking at: %s (%d, %d, %d)", GetBlockName(rayHit.blockType),
                 rayHit.blockX, rayHit.blockY, rayHit.blockZ);
    } else {
        UI::Text("Looking at: ---");
    }

    UI::Separator();
    UI::Text("Position:  %s", cam->GetPosition().ToString().c_str());
    UI::Text("Direction: %s", cam->GetDirection().ToString().c_str());

    float fov = cam->GetFieldOfView();
    if (UI::DragFloat("FOV", &fov, 1.0f, 30.0f, 125.0f))
        cam->SetFieldOfView(fov);

    UI::EndPanel();
}

void HudPanel::RenderToast() {
    float timer = m_scene.GetSaveMessageTimer();
    if (timer <= 0.0f) return;

    float alpha = (timer < 0.5f) ? timer * 2.0f : 1.0f;
    float centerX = UI::GetViewportWidth() * 0.5f - 60.0f;
    UI::BeginPanel("SaveMsg", centerX, 40, 0.5f);
    UI::TextColored(0.2f, 1.0f, 0.2f, alpha, "%s",
                    m_scene.GetSaveMessage().c_str());
    UI::EndPanel();
}

void HudPanel::RenderPerformance() {
    auto* app = Application::GetInstance();
    if (!app) return;

    UI::BeginPanel("Performance", UI::GetViewportWidth() - 200, 0, 0.3f);

    float r, g, b;
    app->GetRendererTypeColor(r, g, b);
    UI::TextColored(r, g, b, 1.0f, "%s", app->GetRendererTypeStr());

    UI::Separator();
    UI::Text("FPS: %d", app->GetFPS());
    UI::Text("Frame Time: %.2f ms", app->GetFrameTime());

    UI::Separator();
    UI::Text("Vertices:  %d", app->GetVertices());
    UI::Text("Triangles: %d", app->GetTriangles());

    UI::Separator();
    UI::Text("CPU: %.1f%%", m_cachedMetrics.CpuUsagePercent);
    UI::Text("RAM: %.1f MB", m_cachedMetrics.RamUsageMB);

    if (m_cachedMetrics.GpuUsagePercent > 0.0f)
        UI::Text("GPU: %.1f%%", m_cachedMetrics.GpuUsagePercent);
    else
        UI::TextDisabled("GPU: N/A");

    size_t vramUsed = app->GetGPUMemoryUsed();
    size_t vramBudget = app->GetGPUMemoryBudget();
    if (vramBudget > 0) {
        float usedMB = static_cast<float>(vramUsed) / (1024.0f * 1024.0f);
        float budgetMB = static_cast<float>(vramBudget) / (1024.0f * 1024.0f);
        float pct =
            (static_cast<float>(vramUsed) / static_cast<float>(vramBudget)) *
            100.0f;
        UI::Text("VRAM: %.0f / %.0f MB (%.0f%%)", usedMB, budgetMB, pct);
    }

    UI::EndPanel();
}
