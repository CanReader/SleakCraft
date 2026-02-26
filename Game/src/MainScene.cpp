#include "MainScene.hpp"
#include <Core/GameObject.hpp>
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
}

void MainScene::Update(float deltaTime) {
    if (deltaTime > 0.05f) deltaTime = 0.05f;
    Scene::Update(deltaTime);

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

        UI::BeginPanel("HUD", 10, 10);

        UI::Text("Selected: %s [%d]", GetBlockName(m_selectedBlock),
                 static_cast<int>(m_selectedBlock));

        auto dir = cam->GetDirection();
        auto rayHit = m_chunkManager.VoxelRaycast(cam->GetPosition(), dir, 6.0f);
        if (rayHit.hit) {
            UI::Text("Looking at: %s (%d, %d, %d)",
                     GetBlockName(rayHit.blockType),
                     rayHit.blockX, rayHit.blockY, rayHit.blockZ);

            constexpr float E = 0.002f;
            Physics::AABB blockAABB(
                Vector3D(rayHit.blockX - E, rayHit.blockY - E, rayHit.blockZ - E),
                Vector3D(rayHit.blockX + 1.0f + E, rayHit.blockY + 1.0f + E, rayHit.blockZ + 1.0f + E));
            DebugLineRenderer::DrawAABB(blockAABB, 0.0f, 0.0f, 0.0f);
        } else {
            UI::Text("Looking at: ---");
        }

        UI::EndPanel();

        UI::BeginPanel("Settings", 10, 100, 0.4f,
                        UI::PanelFlags_NoTitleBar |
                        UI::PanelFlags_AutoResize |
                        UI::PanelFlags_NoMove |
                        UI::PanelFlags_NoFocusOnAppear);
        if (UI::Checkbox("Multithreaded Loading", &m_multithreadedLoading))
            m_chunkManager.SetMultithreaded(m_multithreadedLoading);
        UI::EndPanel();
    }
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
    sun->SetDirection(Vector3D(-0.3f, -0.8f, -0.5f));
    sun->SetColor(1.0f, 0.98f, 0.92f);
    sun->SetIntensity(0.8f);
    sun->SetCastShadows(false);
    sun->SetShadowBias(0.003f);
    sun->SetShadowNormalBias(0.04f);
    sun->SetLightSize(1.5f);
    sun->SetShadowFrustumSize(60.0f);
    sun->SetShadowDistance(100.0f);
    sun->SetShadowNearPlane(0.1f);
    sun->SetShadowFarPlane(150.0f);
    AddObject(sun);

    auto* lm = GetLightManager();
    if (lm) {
        lm->SetAmbientColor(0.6f, 0.65f, 0.75f);
        lm->SetAmbientIntensity(0.3f);
    }
}
