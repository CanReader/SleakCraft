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
        cam->SetPosition({8.0f, PLAYER_EYE_HEIGHT, 8.0f});
        cam->SetFarPlane(1500.0f);
        auto* fpc = cam->GetComponent<FirstPersonController>();
        if (fpc) {
            fpc->SetMaxWalkSpeed(4.3f);
            fpc->SetMaxAcceleration(1000.0f);
            fpc->SetBrakingDeceleration(1000.0f);
            fpc->SetGroundFriction(1.0f);
            fpc->SetJumpZVelocity(0.0f);
        }
        auto* rb = cam->GetComponent<RigidbodyComponent>();
        if (rb) rb->SetUseGravity(false);
    }

    SetupLighting();

    m_chunkManager.Initialize(this, m_blockMaterial);
    m_chunkManager.SetRenderDistance(8);
    m_chunkManager.Update(8.0f, 8.0f);

    return true;
}

void MainScene::Update(float deltaTime) {
    Scene::Update(deltaTime);

    auto* cam = GetDebugCamera();
    if (cam) {
        auto pos = cam->GetPosition();
        if (pos.GetY() != PLAYER_EYE_HEIGHT) {
            cam->SetPosition({pos.GetX(), PLAYER_EYE_HEIGHT, pos.GetZ()});
        }
        m_chunkManager.Update(pos.GetX(), pos.GetZ());
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
    sun->SetCastShadows(true);
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
