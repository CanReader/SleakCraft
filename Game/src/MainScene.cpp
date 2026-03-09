#include "MainScene.hpp"
#include "Game.hpp"
#include "World/TextureAtlas.hpp"
#include <cstring>
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

MainScene::MainScene(const std::string& name, const std::string& savePath,
                     const std::string& worldName, uint32_t seed, bool isNewWorld)
    : Scene(name), m_savePath(savePath), m_worldName(worldName),
      m_worldSeed(seed), m_isNewWorld(isNewWorld) {}

bool MainScene::Initialize() {
    SetupMaterial();
    SetupSkybox();

    Scene::Initialize();

    auto* cam = GetDebugCamera();
    if (cam) {
        cam->SetPosition({8.0f, 70.0f, 8.0f});
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
    }

    SetupLighting();

    m_saveManager.SetSavePath(m_savePath);
    m_chunkManager.Initialize(this, m_blockMaterial);

    if (m_isNewWorld) {
        m_chunkManager.SetSeed(m_worldSeed);

        // Find surface height at spawn and position camera above it
        if (cam) {
            int surfaceY = m_chunkManager.GetGenerator().GetSurfaceHeight(8, 8);
            float spawnY = static_cast<float>(surfaceY) + 2.62f;
            cam->SetPosition({8.0f, spawnY, 8.0f});
        }

        // Load a small area synchronously so the player has ground
        m_chunkManager.SetRenderDistance(3);
        m_chunkManager.Update(cam ? cam->GetPosition().GetX() : 8.0f,
                              cam ? cam->GetPosition().GetY() : 70.0f,
                              cam ? cam->GetPosition().GetZ() : 8.0f);
        m_chunkManager.FlushPendingChunks();
        m_chunkManager.SetRenderDistance(8);
        m_chunkManager.SetMultithreaded(m_multithreadedLoading);

        // Save initial world.dat so it appears in world list
        SaveGame();
    } else {
        LoadGame();
    }

    EventDispatcher::RegisterEventHandler(this, &MainScene::OnMousePressed);
    EventDispatcher::RegisterEventHandler(this, &MainScene::OnKeyPressed);

    return true;
}

bool MainScene::HasUnsavedChanges() const {
    return !m_chunkManager.GetDirtyChunks().empty();
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
        // Prevent placing a block inside the player's bounding box
        float feetY = pos.GetY() - 1.62f;
        bool overlaps = (hit.placeX + 1 > pos.GetX() - 0.3f && hit.placeX < pos.GetX() + 0.3f) &&
                        (hit.placeY + 1 > feetY             && hit.placeY < feetY + 1.8f) &&
                        (hit.placeZ + 1 > pos.GetZ() - 0.3f && hit.placeZ < pos.GetZ() + 0.3f);
        if (!overlaps)
            m_chunkManager.SetBlockAt(hit.placeX, hit.placeY, hit.placeZ, m_selectedBlock);
    }
}

void MainScene::OnKeyPressed(const Events::Input::KeyPressedEvent& e) {
    if_key_press(KEY__1) { m_selectedBlock = BlockType::Grass; }
    if_key_press(KEY__2) { m_selectedBlock = BlockType::Dirt; }
    if_key_press(KEY__3) { m_selectedBlock = BlockType::Stone; }
    if_key_press(KEY__4) { m_selectedBlock = BlockType::Cobblestone; }
    if_key_press(KEY__5) { m_selectedBlock = BlockType::OakLog; }
    if_key_press(KEY__6) { m_selectedBlock = BlockType::DarkOakLog; }
    if_key_press(KEY__7) { m_selectedBlock = BlockType::SpruceLog; }
    if_key_press(KEY__8) { m_selectedBlock = BlockType::OakPlanks; }
    if_key_press(KEY__9) { m_selectedBlock = BlockType::Bricks; }
    if_key_press(KEY__ESCAPE) {
        auto* app = Application::GetInstance();
        if (app) {
            auto* game = static_cast<Game*>(app->GetGame());
            if (game) game->ReturnToMenu();
        }
        return;
    }
    if_key_press(KEY__F3) { m_showUI = !m_showUI; }
    if_key_press(KEY__F5) { SaveGame(); }
    if_key_press(KEY__F6) { LoadGame(); }
}

void MainScene::Update(float deltaTime) {
    if (deltaTime > 0.05f) deltaTime = 0.05f;

    // Frustum cull BEFORE Scene::Update so that inactive chunks
    // don't submit draw commands this frame.
    m_chunkManager.FrustumCull();

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

        m_chunkManager.Update(pos.GetX(), pos.GetY(), pos.GetZ());

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

        // Auto-save
        m_autoSaveTimer += deltaTime;
        if (m_autoSaveTimer >= AUTO_SAVE_INTERVAL) {
            m_autoSaveTimer = 0.0f;
            if (!m_chunkManager.GetDirtyChunks().empty())
                SaveGame();
        }

        // Save/load message fade
        if (m_saveMessageTimer > 0.0f)
            m_saveMessageTimer -= deltaTime;

        if (m_showCrosshair) {
            float cx = UI::GetViewportWidth() * 0.5f;
            float cy = UI::GetViewportHeight() * 0.5f;
            constexpr float arm = 10.0f;
            UI::DrawLine(cx - arm, cy, cx + arm, cy, 1.0f, 1.0f, 1.0f, 0.8f, 2.0f);
            UI::DrawLine(cx, cy - arm, cx, cy + arm, 1.0f, 1.0f, 1.0f, 0.8f, 2.0f);
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

    // --- Save/load feedback ---
    if (m_saveMessageTimer > 0.0f) {
        float alpha = (m_saveMessageTimer < 0.5f) ? m_saveMessageTimer * 2.0f : 1.0f;
        float centerX = UI::GetViewportWidth() * 0.5f - 60.0f;
        UI::BeginPanel("SaveMsg", centerX, 40, 0.5f);
        UI::TextColored(0.2f, 1.0f, 0.2f, alpha, "%s", m_saveMessage.c_str());
        UI::EndPanel();
    }

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

    UI::Checkbox("Show Crosshair", &m_showCrosshair);

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

    UI::Separator();
    float rd = static_cast<float>(m_chunkManager.GetRenderDistance());
    if (UI::DragFloat("Render Distance", &rd, 1.0f, 2.0f, 16.0f))
        m_chunkManager.SetRenderDistance(static_cast<int>(rd));

    UI::EndPanel();
}

void MainScene::SaveGame() {
    auto* cam = GetDebugCamera();
    if (!cam) return;

    WorldMeta meta;
    meta.worldName = m_worldName;
    meta.seed = m_chunkManager.GetSeed();
    auto pos = cam->GetPosition();
    meta.player.posX = pos.GetX();
    meta.player.posY = pos.GetY();
    meta.player.posZ = pos.GetZ();

    auto* fpc = cam->GetComponent<FirstPersonController>();
    if (fpc) {
        meta.player.pitch = fpc->GetPitch();
        meta.player.yaw = fpc->GetYaw();
    }

    meta.player.selectedBlock = static_cast<uint8_t>(m_selectedBlock);
    meta.player.renderDistance = m_chunkManager.GetRenderDistance();

    // Collect dirty chunks
    auto dirtyInfos = m_chunkManager.GetDirtyChunks();
    std::vector<ChunkSaveData> dirtyChunks;
    dirtyChunks.reserve(dirtyInfos.size());
    for (const auto& info : dirtyInfos) {
        ChunkSaveData cd;
        cd.cx = info.cx;
        cd.cy = info.cy;
        cd.cz = info.cz;
        std::memcpy(cd.blocks.data(), info.blockData, 4096);
        dirtyChunks.push_back(std::move(cd));
    }

    if (m_saveManager.SaveWorld(meta, dirtyChunks)) {
        m_chunkManager.ClearDirtyFlags();
        m_saveMessage = "World Saved!";
        m_saveMessageTimer = 2.0f;
    } else {
        m_saveMessage = "Save Failed!";
        m_saveMessageTimer = 3.0f;
    }
}

void MainScene::LoadGame() {
    WorldMeta meta;
    std::unordered_map<int64_t, std::array<uint8_t, 4096>> chunkData;

    if (!m_saveManager.LoadWorld(meta, chunkData)) {
        m_saveMessage = "No Save Found!";
        m_saveMessageTimer = 2.0f;
        return;
    }

    // Restore player state
    auto* cam = GetDebugCamera();
    if (cam) {
        cam->SetPosition({meta.player.posX, meta.player.posY, meta.player.posZ});
        auto* fpc = cam->GetComponent<FirstPersonController>();
        if (fpc) {
            fpc->SetPitch(meta.player.pitch);
            fpc->SetYaw(meta.player.yaw);
        }
        auto* rb = cam->GetComponent<RigidbodyComponent>();
        if (rb)
            rb->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    m_selectedBlock = static_cast<BlockType>(meta.player.selectedBlock);

    // Restore seed and reload all chunks
    m_chunkManager.SetSeed(meta.seed);
    m_chunkManager.LoadChunkData(chunkData);
    m_chunkManager.ForceReload();

    // Load a small area synchronously, let the rest stream in
    m_chunkManager.SetRenderDistance(3);
    m_chunkManager.Update(meta.player.posX, meta.player.posY, meta.player.posZ);
    m_chunkManager.FlushPendingChunks();
    m_chunkManager.SetRenderDistance(8);
    m_chunkManager.SetMultithreaded(m_multithreadedLoading);

    m_saveMessage = "World Loaded!";
    m_saveMessageTimer = 2.0f;
}

void MainScene::SetupMaterial() {
    auto* mat = new Material();
    mat->SetShader("assets/shaders/flat_shader.hlsl");

    // Build runtime atlas from individual block textures
    auto* atlasTex = TextureAtlas::BuildAtlas();
    if (atlasTex) {
        atlasTex->SetFilter(TextureFilter::Nearest);
        atlasTex->SetWrapMode(TextureWrapMode::ClampToEdge);
        mat->SetDiffuseTexture(atlasTex);
    } else {
        // Fallback to old atlas file
        mat->SetDiffuseTexture("assets/textures/block_atlas.png");
        mat->GetDiffuseTexture()->SetFilter(TextureFilter::Nearest);
        mat->GetDiffuseTexture()->SetWrapMode(TextureWrapMode::ClampToEdge);
    }
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
    sun->SetCastShadows(false);
    sun->SetShadowBias(0.002f);
    sun->SetShadowNormalBias(0.03f);
    sun->SetLightSize(6.0f);
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
