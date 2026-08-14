#include "MainScene.hpp"

#include <Events/Event.hpp>
#include <Input/KeyCodes.hpp>

#include <Camera/Camera.hpp>
#include <Core/Application.hpp>
#include <Core/CommandLine.hpp>
#include <Core/GameObject.hpp>
#include <Culling/CullingSystem.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Lighting/DirectionalLight.hpp>
#include <Lighting/LightManager.hpp>
#include <Math/Vector.hpp>
#include <Physics/ColliderComponent.hpp>
#include <Physics/Colliders.hpp>
#include <Physics/RigidbodyComponent.hpp>
#include <Runtime/Material.hpp>
#include <Runtime/Skybox.hpp>
#include <Runtime/Texture.hpp>
#include <UI/UI.hpp>
#include <cmath>
#include <cstring>

#include "Game.hpp"
#include "World/TextureAtlas.hpp"

using namespace Sleak;
using namespace Sleak::Math;

MainScene::MainScene(const std::string& name, const std::string& savePath,
                     const std::string& worldName, uint32_t seed,
                     bool isNewWorld)
    : Scene(name),
      m_savePath(savePath),
      m_worldName(worldName),
      m_worldSeed(seed),
      m_isNewWorld(isNewWorld),
      m_worldPersistence(m_chunkManager, m_saveManager, m_blockEffects,
                          *this),
      m_playerController(m_chunkManager, *this),
      m_blockInteraction(m_chunkManager, m_blockEffects, *this),
      m_hudPanel(m_chunkManager, *this),
      m_settingsPanel(m_chunkManager, *this),
      m_hotbar(m_blockInteraction) {}

bool MainScene::Initialize() {
    SetupMaterial();
    SetupSkybox();

    Scene::Initialize();

    // Create the player camera as a regular scene object
    auto* cam = new Sleak::Camera("PlayerCamera", {8.0f, 70.0f, 8.0f}, 60, 0.1f,
                                  1500.0f);
    cam->SetDirection({0.0f, 0.0f, 1.0f});
    cam->AddComponent<FirstPersonController>();
    cam->AddComponent<Sleak::ColliderComponent>(
        Sleak::Physics::BoundingSphere(Vector3D(0, 0, 0), 0.3f));
    cam->AddComponent<Sleak::RigidbodyComponent>(Sleak::BodyType::Dynamic);
    auto* rb = cam->GetComponent<Sleak::RigidbodyComponent>();
    rb->SetUseGravity(true);
    rb->SetGravity(Vector3D(0, -32.0f, 0));
    AddObject(cam);
    cam->Initialize();
    SetActiveCamera(cam);

    m_playerController.ApplyTuning();

    SetupLighting();

    m_saveManager.SetSavePath(m_savePath);
    m_chunkManager.Initialize(this, m_blockMaterial);
    m_blockEffects.Initialize(this, m_blockMaterial);

    if (m_isNewWorld) {
        m_chunkManager.SetSeed(m_worldSeed);

        // Find surface height at spawn and position camera above it
        if (cam) {
            int surfaceY = m_chunkManager.GetGenerator().GetSurfaceHeight(8, 8);
            float spawnY = static_cast<float>(surfaceY) + 2.62f;
            cam->SetPosition({8.0f, spawnY, 8.0f});
        }

        // Load a small area synchronously so the player has ground
        m_chunkManager.SetRenderDistance(4);
        m_chunkManager.Update(cam ? cam->GetPosition().GetX() : 8.0f,
                              cam ? cam->GetPosition().GetY() : 70.0f,
                              cam ? cam->GetPosition().GetZ() : 8.0f);
        m_chunkManager.FlushPendingChunks();
        {
            const std::string rdStr = Sleak::CommandLine::GetValue("-rd");
            int cliRD = rdStr.empty() ? 0 : std::stoi(rdStr);
            int rd = cliRD > 0 ? cliRD : 12;
            if (rd > 16) rd = 16;
            m_chunkManager.SetRenderDistance(rd);
        }
        m_chunkManager.SetMultithreaded(m_multithreadedLoading);

        // Save initial world.dat so it appears in world list
        SaveGame();
    } else {
        LoadGame();
    }

    if (auto* lm = GetLightManager()) {
        float fogDist = m_chunkManager.GetDrawDistance();
        lm->SetFogDistances(fogDist * 0.91f, fogDist);
    }

    // Register game-specific benchmark metrics
    auto* app = Sleak::Application::GetInstance();
    if (app && app->GetBenchmark()) {
        app->GetBenchmark()->RegisterMetric("RenderDistance", [this]() {
            return static_cast<float>(m_chunkManager.GetRenderDistance());
        });
        app->GetBenchmark()->RegisterMetric("IsMoving", [this]() {
            auto* cam = GetActiveCamera();
            if (!cam) return 0.0f;
            auto* rb = cam->GetComponent<RigidbodyComponent>();
            if (!rb) return 0.0f;
            auto vel = rb->GetVelocity();
            return (vel.Magnitude() > 0.01f) ? 1.0f : 0.0f;
        });
        app->GetBenchmark()->RegisterMetric("VRAM_MB", [app]() {
            return static_cast<float>(app->GetGPUMemoryUsed()) /
                   (1024.0f * 1024.0f);
        });
        app->GetBenchmark()->RegisterMetric("VRAM_Pct", [app]() {
            size_t budget = app->GetGPUMemoryBudget();
            if (budget == 0) return 0.0f;
            return (static_cast<float>(app->GetGPUMemoryUsed()) /
                    static_cast<float>(budget)) *
                   100.0f;
        });
        app->GetBenchmark()->RegisterMetric("Cull_Tested", []() {
            return static_cast<float>(Sleak::CullingSystem::GetStats().tested);
        });
        app->GetBenchmark()->RegisterMetric("Cull_Frustum", []() {
            return static_cast<float>(
                Sleak::CullingSystem::GetStats().frustumCulled);
        });
        app->GetBenchmark()->RegisterMetric("Cull_Occlusion", []() {
            return static_cast<float>(
                Sleak::CullingSystem::GetStats().occlusionCulled);
        });
        app->GetBenchmark()->RegisterMetric("Cull_RasterMs", []() {
            return Sleak::CullingSystem::GetStats().rasterizeMs;
        });
    }

    m_windowCloseHandlerId =
        EventDispatcher::RegisterEventHandler(this, &MainScene::OnWindowClose);
    m_mousePressedHandlerId =
        EventDispatcher::RegisterEventHandler(this, &MainScene::OnMousePressed);
    m_mouseScrolledHandlerId = EventDispatcher::RegisterEventHandler(
        this, &MainScene::OnMouseScrolled);
    m_keyPressedHandlerId =
        EventDispatcher::RegisterEventHandler(this, &MainScene::OnKeyPressed);
    m_keyReleasedHandlerId =
        EventDispatcher::RegisterEventHandler(this, &MainScene::OnKeyReleased);

    return true;
}

MainScene::~MainScene() {
    // Unregister anything that OnDeactivate may not have caught
    EventDispatcher::UnregisterEvent(EventType::WindowClose,
                                     m_windowCloseHandlerId);
    EventDispatcher::UnregisterEvent(EventType::MousePressed,
                                     m_mousePressedHandlerId);
    EventDispatcher::UnregisterEvent(EventType::MouseScrolled,
                                     m_mouseScrolledHandlerId);
    EventDispatcher::UnregisterEvent(EventType::KeyPressed,
                                     m_keyPressedHandlerId);
    EventDispatcher::UnregisterEvent(EventType::KeyReleased,
                                     m_keyReleasedHandlerId);
    UnregisterBenchmarkMetrics();
}

void MainScene::OnDeactivate() {
    Scene::OnDeactivate();
    EventDispatcher::UnregisterEvent(EventType::WindowClose,
                                     m_windowCloseHandlerId);
    EventDispatcher::UnregisterEvent(EventType::MousePressed,
                                     m_mousePressedHandlerId);
    EventDispatcher::UnregisterEvent(EventType::MouseScrolled,
                                     m_mouseScrolledHandlerId);
    EventDispatcher::UnregisterEvent(EventType::KeyPressed,
                                     m_keyPressedHandlerId);
    EventDispatcher::UnregisterEvent(EventType::KeyReleased,
                                     m_keyReleasedHandlerId);
    m_windowCloseHandlerId.clear();
    m_mousePressedHandlerId.clear();
    m_mouseScrolledHandlerId.clear();
    m_keyPressedHandlerId.clear();
    m_keyReleasedHandlerId.clear();
    UnregisterBenchmarkMetrics();
}

void MainScene::UnregisterBenchmarkMetrics() {
    // Metric lambdas capture `this`; the Benchmark outlives the scene
    auto* app = Sleak::Application::GetInstance();
    if (app && app->GetBenchmark()) {
        app->GetBenchmark()->UnregisterMetric("RenderDistance");
        app->GetBenchmark()->UnregisterMetric("IsMoving");
        app->GetBenchmark()->UnregisterMetric("VRAM_MB");
        app->GetBenchmark()->UnregisterMetric("VRAM_Pct");
    }
}

bool MainScene::HasUnsavedChanges() const {
    return !m_chunkManager.GetDirtyChunks().empty();
}

void MainScene::OnMousePressed(
    const Events::Input::MouseButtonPressedEvent& e) {
    m_blockInteraction.OnMousePressed(e);
}

void MainScene::OnMouseScrolled(const Events::Input::MouseScrolledEvent& e) {
    m_blockInteraction.OnMouseScrolled(e);
}

void MainScene::OnKeyPressed(const Events::Input::KeyPressedEvent& e) {
    m_playerController.OnKeyPressed(e);
    m_blockInteraction.OnKeyPressed(e);

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
    if_key_press(KEY__F6) {
        // Double-press confirm: F6 sits next to F5 and discards unsaved edits
        if (m_loadConfirmTimer > 0.0f ||
            m_chunkManager.GetDirtyChunks().empty()) {
            m_loadConfirmTimer = 0.0f;
            LoadGame();
        } else {
            m_saveMessage = "Unsaved changes! Press F6 again to reload.";
            m_saveMessageTimer = 3.0f;
            m_loadConfirmTimer = 3.0f;
        }
    }
}

void MainScene::OnKeyReleased(const Events::Input::KeyReleasedEvent& e) {
    m_playerController.OnKeyReleased(e);
}

void MainScene::Update(float deltaTime) {
    if (deltaTime > 0.05f) deltaTime = 0.05f;
    m_gameTime += deltaTime;

    auto* cam = GetActiveCamera();
    if (cam) {
        m_blockEffects.Update(deltaTime, cam->GetPosition());
        m_blockInteraction.DrainCompletedPlacements();
    }

    Scene::Update(deltaTime);

    m_hudPanel.Update(deltaTime);

    if (cam) {
        auto pos = cam->GetPosition();

        m_playerController.Update();

        m_chunkManager.Update(pos.GetX(), pos.GetY(), pos.GetZ());
        m_chunkManager.RenderColumns();

        // Animate water — pass game time through material tiling.x
        if (m_waterMaterial) {
            m_waterMaterial->SetTiling(m_gameTime, 1.0f);
        }
        m_chunkManager.RenderWater();

        m_blockInteraction.RenderOutline();

        // Auto-save
        m_autoSaveTimer += deltaTime;
        if (m_autoSaveTimer >= AUTO_SAVE_INTERVAL) {
            m_autoSaveTimer = 0.0f;
            if (!m_chunkManager.GetDirtyChunks().empty()) SaveGame();
        }

        // Save/load message fade
        if (m_saveMessageTimer > 0.0f) m_saveMessageTimer -= deltaTime;
        if (m_loadConfirmTimer > 0.0f) m_loadConfirmTimer -= deltaTime;

        m_hudPanel.RenderCrosshair();

        if (m_showUI) RenderUI();

        m_hotbar.Render();
    }
}

void MainScene::RenderUI() {
    m_hudPanel.Render();
    m_settingsPanel.Render();
}

void MainScene::OnWindowClose(const Sleak::Events::WindowCloseEvent&) {
    // Renderer is still alive here — last safe point to persist the world
    if (!m_chunkManager.GetDirtyChunks().empty()) SaveGame();
}

void MainScene::SaveGame() { m_worldPersistence.SaveGame(); }

void MainScene::LoadGame() { m_worldPersistence.LoadGame(); }

void MainScene::SetupMaterial() {
    auto* mat = new Material();
    mat->SetShader("assets/shaders/flat_shader.hlsl");

    Sleak::TextureFilter texFilter = m_settingsPanel.GetTextureFilter();

    // Build runtime atlas from individual block textures
    auto* atlasTex = TextureAtlas::BuildAtlas();
    if (atlasTex) {
        atlasTex->SetFilter(texFilter);
        atlasTex->SetWrapMode(TextureWrapMode::ClampToEdge);
        mat->SetDiffuseTexture(atlasTex);
    } else {
        // Fallback to old atlas file
        mat->SetDiffuseTexture("assets/textures/block_atlas.png");
        mat->GetDiffuseTexture()->SetFilter(texFilter);
        mat->GetDiffuseTexture()->SetWrapMode(TextureWrapMode::ClampToEdge);
    }
    mat->SetDiffuseColor((uint8_t)255, (uint8_t)255, (uint8_t)255);
    mat->SetSpecularColor((uint8_t)0, (uint8_t)0, (uint8_t)0);
    mat->SetShininess(0.0f);
    mat->SetMetallic(0.0f);
    mat->SetRoughness(0.85f);
    mat->SetAO(1.0f);
    mat->SetOpacity(1.0f);
    mat->Initialize();
    m_blockMaterial = RefPtr<Material>(mat);

    // Water material
    auto* waterMat = new Material();
    waterMat->SetShader("assets/shaders/water_shader.hlsl");
    auto* waterAtlasTex = TextureAtlas::BuildAtlas();
    if (waterAtlasTex) {
        waterAtlasTex->SetFilter(texFilter);
        waterAtlasTex->SetWrapMode(TextureWrapMode::ClampToEdge);
        waterMat->SetDiffuseTexture(waterAtlasTex);
    }
    waterMat->SetDiffuseColor((uint8_t)255, (uint8_t)255, (uint8_t)255);
    waterMat->SetOpacity(0.7f);
    waterMat->SetRenderMode(MaterialRenderMode::Transparent);
    waterMat->SetTwoSided(true);
    waterMat->SetShininess(64.0f);
    waterMat->SetSpecularColor((uint8_t)255, (uint8_t)255, (uint8_t)255);
    waterMat->Initialize();
    m_waterMaterial = RefPtr<Material>(waterMat);
    m_chunkManager.SetWaterMaterial(m_waterMaterial);
}

void MainScene::SetupSkybox() {
    auto* skybox = new Skybox();
    SetSkybox(skybox);
}

void MainScene::SetupLighting() {
    m_sun = new DirectionalLight("Sun");
    m_sun->SetDirection(m_settingsPanel.ComputeSunDirection());
    float sunR, sunG, sunB;
    m_settingsPanel.GetSunColor(sunR, sunG, sunB);
    m_sun->SetColor(sunR, sunG, sunB);
    m_sun->SetIntensity(m_settingsPanel.GetSunIntensity());
    m_sun->SetLightSize(4.0f);
    m_settingsPanel.AttachSun(m_sun);

    // Voxel-tuned graphics config: shadows on, no SSAO/SSR/IBL/bloom.
    Sleak::GraphicsConfig cfg =
        Sleak::GraphicsConfig::Preset(Sleak::GraphicsQuality::Medium);
    cfg.ssaoEnabled = false;
    cfg.ssrEnabled = false;
    cfg.iblEnabled = false;
    cfg.bloomEnabled = false;  // match OpenGL ref (no post-FX bloom chain)
    cfg.taaEnabled = true;     // smooths shadow-edge aliasing (VK only for now)
    cfg.shadowMapResolution = 3072;  // BSL ULTRA tier; 4096 spiked VK frames
    cfg.shadowFrustumSize = 128.0f;  // half-extent → 256m ≈ fog range; 2x texel density
    cfg.shadowCasterDistance = 256.0f;
    cfg.shadowDistance =
        400.0f;  // pull-back > frustum, else near-clip eats shadows
    cfg.frustumCullingEnabled = m_frustumCulling;
    cfg.occlusionCullingEnabled = m_occlusionCulling;
    cfg.occlusionBufferWidth = 256;
    cfg.occlusionBufferHeight = 144;
    cfg.maxOccluders = 192;

    auto* app = Sleak::Application::GetInstance();
    if (app) app->ApplyGraphicsConfig(cfg);

    // Apply culling settings directly (not carried by ApplyGraphicsConfig).
    Sleak::CullingSystem::SetOcclusionBufferSize(cfg.occlusionBufferWidth,
                                                 cfg.occlusionBufferHeight);
    Sleak::CullingSystem::SetMaxOccluders(cfg.maxOccluders);
    m_chunkManager.SetCullingEnabled(cfg.frustumCullingEnabled,
                                     cfg.occlusionCullingEnabled);
    cfg.ApplyShadows(*m_sun);
    m_chunkManager.SetShadowCasterDistance(cfg.shadowCasterDistance);

    // Settings not carried by the config — applied directly.
    m_sun->SetShadowNormalBias(0.25f);  // ~3 texels at 3072/256m — covers 5-texel PCF
    m_sun->SetShadowNearPlane(0.1f);
    m_sun->SetShadowFarPlane(900.0f);
    AddObject(m_sun);

    auto* lm = GetLightManager();
    if (lm) {
        float ambR, ambG, ambB;
        m_settingsPanel.GetAmbientColor(ambR, ambG, ambB);
        lm->SetAmbientColor(ambR, ambG, ambB);
        lm->SetAmbientIntensity(m_settingsPanel.GetAmbientIntensity());

        float horizonR, horizonG, horizonB;
        m_settingsPanel.GetFogHorizonColor(horizonR, horizonG, horizonB);
        lm->SetFogColor(horizonR, horizonG, horizonB);
        float zenithR, zenithG, zenithB;
        m_settingsPanel.GetFogZenithColor(zenithR, zenithG, zenithB);
        lm->SetFogZenithColor(zenithR, zenithG, zenithB);
        float fogDist = m_chunkManager.GetDrawDistance();
        lm->SetFogDistances(fogDist * 0.9f, fogDist);
        lm->SetFogEnabled(m_settingsPanel.IsFogEnabled());

        lm->SetHeightFogEnabled(m_settingsPanel.IsHeightFogEnabled());
        lm->SetHeightFogTop(m_settingsPanel.GetHeightFogTop());
        lm->SetHeightFogDensity(m_settingsPanel.GetHeightFogDensity());
        lm->SetHeightFogFalloff(m_settingsPanel.GetHeightFogFalloff());
    }
}
