#include "Player/PlayerController.hpp"

#include "MainScene.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Input/KeyCodes.hpp>
#include <Math/Vector.hpp>
#include <Physics/RigidbodyComponent.hpp>

using namespace Sleak;

void PlayerController::ApplyTuning() {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;
    auto* fpc = cam->GetComponent<FirstPersonController>();
    if (fpc) {
        // Walk + sprint
        fpc->SetMaxWalkSpeed(4.317f);
        fpc->SetSprintSpeedMultiplier(1.3f);
        fpc->SetMaxAcceleration(35.0f);
        fpc->SetBrakingDeceleration(20.0f);
        fpc->SetGroundFriction(6.0f);
        fpc->SetAirControl(0.2f);
        fpc->SetJumpZVelocity(8.4f);
        // Fly
        fpc->SetMaxFlySpeed(m_flySpeed);
        fpc->SetFlySprintMultiplier(m_flySprintMultiplier);
        fpc->SetPitch(0.0f);
        fpc->SetYaw(0.0f);
        fpc->SetEnabled(true);
    }
}

void PlayerController::OnKeyPressed(
    const Events::Input::KeyPressedEvent& e) {
    // Minecraft-style: double-tap space toggles fly on/off
    if (e.GetKeyCode() == Input::KEY_CODE::KEY__SPACE) {
        m_spaceHeld = true;
        if (!e.IsRepeat()) {
            float now = m_scene.GetGameTime();
            if ((now - m_lastSpacePressTime) < DOUBLE_TAP_WINDOW) {
                m_flying = !m_flying;
                auto* cam = m_scene.GetActiveCamera();
                auto* rb =
                    cam ? cam->GetComponent<RigidbodyComponent>() : nullptr;
                auto* fpc =
                    cam ? cam->GetComponent<FirstPersonController>() : nullptr;
                if (rb) {
                    rb->SetUseGravity(!m_flying);
                    if (m_flying) rb->SetVelocity({0.0f, 0.0f, 0.0f});
                }
                if (fpc) fpc->SetFlying(m_flying);
                m_lastSpacePressTime =
                    -1.0f;  // reset so triple-tap doesn't re-toggle
            } else {
                m_lastSpacePressTime = now;
            }
        }
    }

    if (e.GetKeyCode() == Input::KEY_CODE::KEY__LCTRL ||
        e.GetKeyCode() == Input::KEY_CODE::KEY__RCTRL) {
        m_ctrlHeld = true;
    }
    if (e.GetKeyCode() == Input::KEY_CODE::KEY__LSHIFT ||
        e.GetKeyCode() == Input::KEY_CODE::KEY__RSHIFT) {
        m_shiftHeld = true;
    }
}

void PlayerController::OnKeyReleased(
    const Events::Input::KeyReleasedEvent& e) {
    if (e.GetKeyCode() == Input::KEY_CODE::KEY__SPACE) m_spaceHeld = false;
    if (e.GetKeyCode() == Input::KEY_CODE::KEY__LCTRL ||
        e.GetKeyCode() == Input::KEY_CODE::KEY__RCTRL)
        m_ctrlHeld = false;
    if (e.GetKeyCode() == Input::KEY_CODE::KEY__LSHIFT ||
        e.GetKeyCode() == Input::KEY_CODE::KEY__RSHIFT)
        m_shiftHeld = false;
}

void PlayerController::Update() {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;

    // Fly: space=up, ctrl=down, shift=sprint
    if (m_flying) {
        auto* rb = cam->GetComponent<RigidbodyComponent>();
        auto* fpc = cam->GetComponent<FirstPersonController>();
        float v = 0.0f;
        if (m_spaceHeld) v += 1.0f;
        if (m_ctrlHeld) v -= 1.0f;
        if (fpc) fpc->SetVerticalFlyInput(v);
        if (rb) {
            rb->SetVelocity({0.0f, 0.0f, 0.0f});
            rb->SetGrounded(false);
        }
    } else {
        auto* fpc = cam->GetComponent<FirstPersonController>();
        if (fpc) fpc->SetVerticalFlyInput(0.0f);
    }

    // Collision resolution (always active)
    {
        auto curPos = cam->GetPosition();
        auto collision =
            m_chunkManager.ResolveVoxelCollision(curPos, 0.3f, 1.8f, 1.62f);
        if (collision.onGround || collision.hitCeiling ||
            collision.hitWall) {
            cam->SetPosition({curPos.GetX() + collision.correction.GetX(),
                              curPos.GetY() + collision.correction.GetY(),
                              curPos.GetZ() + collision.correction.GetZ()});
            if (!m_flying) {
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
                        float vx = (collision.correction.GetX() != 0.0f)
                                       ? 0.0f
                                       : vel.GetX();
                        float vz = (collision.correction.GetZ() != 0.0f)
                                       ? 0.0f
                                       : vel.GetZ();
                        rb->SetVelocity({vx, vel.GetY(), vz});
                    }
                }
            }
        }
    }
}
