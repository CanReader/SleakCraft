#include "MainScene.hpp"
#include <Core/GameObject.hpp>
#include <Core/Application.hpp>
#include <Math/Vector.hpp>
#include <Lighting/DirectionalLight.hpp>
#include <Lighting/LightManager.hpp>
#include <Runtime/Material.hpp>
#include <Runtime/Texture.hpp>
#include <Runtime/Skybox.hpp>
#include <Camera/Camera.hpp>
#include <Physics/RigidbodyComponent.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Events/Event.h>
#include <Input/KeyCodes.h>
#include <UI/UI.hpp>
#include <Debug/DebugLineRenderer.hpp>
#include <Physics/Colliders.hpp>

using namespace Sleak;
using namespace Sleak::Math;

MainScene::MainScene(const std::string& name)
    : Scene(name) {}

bool MainScene::Initialize() {
    SetupMaterial();
    SetupSkybox();

    Scene::Initialize();

    auto* cam = GetDebugCamera();
    if (cam) {
        cam->SetPosition({8.0f, 6.62f, 8.0f});
        cam->SetDirection({0.0f, 0.0f, 1.0f});
        cam->SetFarPlane(1500.0f);
        auto* fpc = cam->GetComponent<FirstPersonController>();
        if (fpc) {
            fpc->SetMaxWalkSpeed(4.3f);
            fpc->SetMaxAcceleration(1000.0f);
            fpc->SetBrakingDeceleration(1000.0f);
            fpc->SetGroundFriction(1.0f);
            fpc->SetJumpZVelocity(fpc->GetJumpZVelocity()*1.8);
            fpc->SetPitch(0.0f);
            fpc->SetYaw(0.0f);
        }
        auto* rb = cam->GetComponent<RigidbodyComponent>();
    }

    SetupLighting();

    m_chunkManager.Initialize(this, m_blockMaterial);
    m_chunkManager.SetRenderDistance(8);
    m_chunkManager.Update(8.0f, 8.0f);
    m_chunkManager.FlushPendingChunks();
    m_chunkManager.SetMultithreaded(m_multithreadedLoading);

    EventDispatcher::RegisterEventHandler(this, &MainScene::OnMousePressed);
    EventDispatcher::RegisterEventHandler(this, &MainScene::OnKeyPressed);

    return true;
}

void MainScene::OnMousePressed(const Events::Input::MouseButtonPressedEvent& e) {
    auto* cam = GetDebugCamera();
    if (!cam) return;

    auto pos = cam->GetPosition();
    auto dir = cam->GetDirection();
    auto hit = m_chunkManager.VoxelRaycast(pos, dir, 6.0f);
    if (!hit.hit) return;

    MouseCode button = e.GetMouseButton();
    if (button == MouseCode::ButtonLeft) {
        m_chunkManager.SetBlockAt(hit.blockX, hit.blockY, hit.blockZ, BlockType::Air);
    } else if (button == MouseCode::ButtonRight) {
        m_chunkManager.SetBlockAt(hit.placeX, hit.placeY, hit.placeZ, m_selectedBlock);
    }
}

void MainScene::OnKeyPressed(const Events::Input::KeyPressedEvent& e) {
    if_key_press(KEY__1) { m_selectedBlock = BlockType::Grass; }
    if_key_press(KEY__2) { m_selectedBlock = BlockType::Dirt; }
    if_key_press(KEY__3) { m_selectedBlock = BlockType::Stone; }
    if_key_press(KEY__F3) { m_showUI = !m_showUI; }
}

void MainScene::Update(float deltaTime) {
    if (deltaTime > 0.05f) deltaTime = 0.05f;
    Scene::Update(deltaTime);

    // Refresh cached metrics
    m_metricTimer += deltaTime;
    if (m_metricTimer >= 0.5f) {
        m_cachedMetrics = SystemMetrics::Query();
        m_metricTimer = 0.0f;
    }

    auto* cam = GetDebugCamera();
    if (cam) {
        auto pos = cam->GetPosition();

        auto collision = m_chunkManager.ResolveVoxelCollision(pos, 0.3f, 1.8f, 1.62f);
        if (collision.onGround || collision.hitCeiling || collision.hitWall) {
            cam->SetPosition({pos.GetX() + collision.correction.GetX(),
                              pos.GetY() + collision.correction.GetY(),
                              pos.GetZ() + collision.correction.GetZ()});
            auto* rb = cam->GetComponent<RigidbodyComponent>();
            if (rb) {
                auto vel = rb->GetVelocity();
                if (collision.onGround && vel.GetY() < 0.0f) {
                    rb->SetVelocity({vel.GetX(), 0.0f, vel.GetZ()});
                    rb->SetGrounded(true);
                }
                if (collision.hitCeiling && vel.GetY() > 0.0f)
                    rb->SetVelocity({vel.GetX(), 0.0f, vel.GetZ()});
                if (collision.hitWall) {
                    float vx = (collision.correction.GetX() != 0.0f) ? 0.0f : vel.GetX();
                    float vz = (collision.correction.GetZ() != 0.0f) ? 0.0f : vel.GetZ();
                    rb->SetVelocity({vx, vel.GetY(), vz});
                }
            }
        }

        m_chunkManager.Update(pos.GetX(), pos.GetZ());

        // Block outline always visible
        auto dir = cam->GetDirection();
        auto rayHit = m_chunkManager.VoxelRaycast(cam->GetPosition(), dir, 6.0f);
        if (rayHit.hit) {
            constexpr float E = 0.002f;
            Physics::AABB blockAABB(
                Vector3D(rayHit.blockX - E, rayHit.blockY - E, rayHit.blockZ - E),
                Vector3D(rayHit.blockX + 1.0f + E, rayHit.blockY + 1.0f + E, rayHit.blockZ + 1.0f + E));
            DebugLineRenderer::DrawAABB(blockAABB, 0.0f, 0.0f, 0.0f);
        }

        if (m_showUI)
            RenderUI();
    }
}

void MainScene::RenderUI() {
    auto* cam = GetDebugCamera();
    auto* app = Application::GetInstance();
    if (!cam || !app) return;

    // --- HUD panel (top-left) ---
    UI::BeginPanel("HUD", 0, 0);

    UI::Text("Selected: %s [%d]", GetBlockName(m_selectedBlock),
             static_cast<int>(m_selectedBlock));

    auto dir = cam->GetDirection();
    auto rayHit = m_chunkManager.VoxelRaycast(cam->GetPosition(), dir, 6.0f);
    if (rayHit.hit) {
        UI::Text("Looking at: %s (%d, %d, %d)",
                 GetBlockName(rayHit.blockType),
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

    // --- Performance panel (top-right) ---
    UI::BeginPanel("Performance", UI::GetViewportWidth() - 200, 0,
                   0.3f);

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

    UI::EndPanel();

    // --- Settings panel (below HUD) ---
    UI::BeginPanel("Settings", 0, 120, 0.4f,
                    UI::PanelFlags_NoTitleBar |
                    UI::PanelFlags_AutoResize |
                    UI::PanelFlags_NoMove |
                    UI::PanelFlags_NoFocusOnAppear);

    if (UI::Checkbox("Multithreaded Loading", &m_multithreadedLoading))
        m_chunkManager.SetMultithreaded(m_multithreadedLoading);

    if (UI::Checkbox("Show Colliders", &m_showColliders))
        DebugLineRenderer::SetEnabled(m_showColliders);

    UI::Separator();
    UI::Text("Anti-Aliasing");
    {
        const char* labels[] = {"Off", "2x", "4x", "8x"};
        int values[] = {1, 2, 4, 8};
        int count = 1;
        uint32_t maxMSAA = app->GetMaxMSAASampleCount();
        for (int i = 1; i < 4; i++)
            if (static_cast<uint32_t>(values[i]) <= maxMSAA) count = i + 1;
        int current = 0;
        for (int i = 0; i < count; i++)
            if (static_cast<uint32_t>(values[i]) == app->GetMSAASampleCount()) current = i;
        if (UI::Combo("MSAA", &current, labels, count))
            app->SetMSAASampleCount(values[current]);
    }

    UI::EndPanel();
}

void MainScene::SetupMaterial() {
    auto* mat = new Material();
    mat->SetShader("assets/shaders/flat_shader.hlsl");
    mat->SetDiffuseTexture("assets/textures/block_atlas.png");
    mat->GetDiffuseTexture()->SetFilter(TextureFilter::Nearest);
    mat->GetDiffuseTexture()->SetWrapMode(TextureWrapMode::ClampToEdge);
    mat->SetDiffuseColor((uint8_t)255, (uint8_t)255, (uint8_t)255);
    mat->SetSpecularColor((uint8_t)0, (uint8_t)0, (uint8_t)0);
    mat->SetShininess(0.0f);
    mat->SetMetallic(0.0f);
    mat->SetRoughness(0.0f);
    mat->SetAO(1.0f);
    mat->SetOpacity(1.0f);
    m_blockMaterial = RefPtr<Material>(mat);
}

void MainScene::SetupSkybox() {
    auto* skybox = new Skybox();
    SetSkybox(skybox);
}

void MainScene::SetupLighting() {
    auto* sun = new DirectionalLight("Sun");
    sun->SetDirection(Vector3D(-0.35f, -0.75f, -0.45f));
    sun->SetColor(1.0f, 0.95f, 0.85f);
    sun->SetIntensity(0.85f);
    sun->SetCastShadows(true);
    sun->SetShadowBias(0.002f);
    sun->SetShadowNormalBias(0.03f);
    sun->SetLightSize(3.0f);
    sun->SetShadowFrustumSize(80.0f);
    sun->SetShadowDistance(120.0f);
    sun->SetShadowNearPlane(0.1f);
    sun->SetShadowFarPlane(200.0f);
    AddObject(sun);

    auto* lm = GetLightManager();
    if (lm) {
        lm->SetAmbientColor(0.45f, 0.52f, 0.65f);
        lm->SetAmbientIntensity(0.2f);
    }
}
