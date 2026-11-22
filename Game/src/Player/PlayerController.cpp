#include "Player/PlayerController.hpp"

#include "MainScene.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Input/KeyCodes.hpp>
#include <Math/Math.hpp>
#include <Math/Vector.hpp>
#include <Physics/RigidbodyComponent.hpp>
#include <algorithm>
#include <cmath>

using namespace Sleak;

namespace {
// Autopilot view, about 27 degrees below its +Z heading
#define AUTOFLY_CLEARANCE 8.0f
const Math::Vector3D kAutoFlyLook(0.0f, -0.5f, 1.0f);

// Player box around the eye
constexpr float PLAYER_HALF_WIDTH = 0.3f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr float PLAYER_EYE_HEIGHT = 1.62f;

// Swept collision: longest per-axis substep, most substeps per frame
constexpr float MAX_COLLISION_STEP = 0.25f;
constexpr int MAX_COLLISION_SUBSTEPS = 16;
static_assert(MAX_COLLISION_STEP < PLAYER_HALF_WIDTH,
              "a substep must sink less than the half-width so MTV picks Y");

/// Voxel collision merged over the substeps of one frame's motion.
struct CollisionSweep {
    Math::Vector3D position;
    bool onGround = false;
    bool hitCeiling = false;
    bool hitWall = false;
    bool correctedX = false;
    bool correctedZ = false;
};

/// Resolves collision at substeps from `from` to `to`; each substep starts
/// from the previous one's corrections, so it sinks at most one step deep.
CollisionSweep SweepVoxelCollision(const ChunkManager& chunks,
                                   const Math::Vector3D& from,
                                   const Math::Vector3D& to) {
    const Math::Vector3D delta = to - from;
    const float needed = std::ceil(
        std::max({std::fabs(delta.GetX()), std::fabs(delta.GetY()),
                  std::fabs(delta.GetZ())}) /
        MAX_COLLISION_STEP);
    // NaN or beyond any frame's reach: a teleport, resolved in place
    const bool teleport = !std::isfinite(needed) ||
                          needed > static_cast<float>(MAX_COLLISION_SUBSTEPS);
    const int steps = teleport ? 1 : std::max(1, static_cast<int>(needed));

    CollisionSweep sweep;
    Math::Vector3D correction;
    for (int i = 1; i <= steps; ++i) {
        // Last substep lands on the unresolved position plus corrections
        const Math::Vector3D path =
            i == steps ? to
                       : from + delta * (static_cast<float>(i) /
                                         static_cast<float>(steps));
        const VoxelCollisionResult hit = chunks.ResolveVoxelCollision(
            path + correction, PLAYER_HALF_WIDTH, PLAYER_HEIGHT,
            PLAYER_EYE_HEIGHT);
        correction += hit.correction;
        sweep.onGround = sweep.onGround || hit.onGround;
        sweep.hitCeiling = sweep.hitCeiling || hit.hitCeiling;
        sweep.hitWall = sweep.hitWall || hit.hitWall;
        sweep.correctedX = sweep.correctedX || hit.correction.GetX() != 0.0f;
        sweep.correctedZ = sweep.correctedZ || hit.correction.GetZ() != 0.0f;
    }
    sweep.position = to + correction;
    return sweep;
}
}  // namespace

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

void PlayerController::StartAutoFly(float speed) {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam || speed <= 0.0f) return;
    m_autoFlySpeed = speed;

    // Above the build limit
    const float altitude =
        static_cast<float>((WorldGenerator::MAX_CHUNK_Y + 1) * Chunk::SIZE) +
        AUTOFLY_CLEARANCE;
    auto pos = cam->GetPosition();
    cam->SetPosition({pos.GetX(), altitude, pos.GetZ()});
    cam->SetDirection(kAutoFlyLook);

    if (auto* fpc = cam->GetComponent<FirstPersonController>())
        fpc->SetEnabled(false);
    if (auto* rb = cam->GetComponent<RigidbodyComponent>())
        rb->SetUseGravity(false);
}

void PlayerController::PinCamera(const CameraPose& pose) {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;
    const float yaw = static_cast<float>(pose.yawDeg * D2R);
    const float pitch = static_cast<float>(pose.pitchDeg * D2R);
    m_pinPosition = Math::Vector3D(pose.x, pose.y, pose.z);
    m_pinDirection =
        Math::Vector3D(std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                       std::cos(yaw) * std::cos(pitch));
    m_pinned = true;

    if (auto* fpc = cam->GetComponent<FirstPersonController>())
        fpc->SetEnabled(false);
    if (auto* rb = cam->GetComponent<RigidbodyComponent>())
        rb->SetUseGravity(false);
}

void PlayerController::Update(const Math::Vector3D& sweepStart) {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;

    // Pinned capture pose
    if (m_pinned) {
        if (auto* rb = cam->GetComponent<RigidbodyComponent>()) {
            rb->SetUseGravity(false);
            rb->SetVelocity(Math::Vector3D());
        }
        cam->SetPosition(m_pinPosition);
        cam->SetDirection(m_pinDirection);
        return;
    }

    // Autopilot: physics integrates velocity
    if (m_autoFlySpeed > 0.0f) {
        if (auto* rb = cam->GetComponent<RigidbodyComponent>()) {
            rb->SetUseGravity(false);
            rb->SetVelocity(Math::Vector3D::Forward() * m_autoFlySpeed);
        }
        cam->SetDirection(kAutoFlyLook);
        return;
    }

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

    // Collision resolution (always active), swept from the frame's start
    {
        const CollisionSweep collision =
            SweepVoxelCollision(m_chunkManager, sweepStart, cam->GetPosition());
        if (collision.onGround || collision.hitCeiling ||
            collision.hitWall) {
            cam->SetPosition(collision.position);
            if (!m_flying) {
                auto* rb = cam->GetComponent<RigidbodyComponent>();
                if (rb) {
                    const auto vel = rb->GetVelocity();
                    float vx = vel.GetX();
                    float vy = vel.GetY();
                    float vz = vel.GetZ();
                    if (collision.onGround && vy < 0.0f) {
                        vy = 0.0f;
                        rb->SetGrounded(true);
                    }
                    if (collision.hitCeiling && vy > 0.0f) vy = 0.0f;
                    if (collision.hitWall) {
                        if (collision.correctedX) vx = 0.0f;
                        if (collision.correctedZ) vz = 0.0f;
                    }
                    rb->SetVelocity({vx, vy, vz});
                }
            }
        }
    }
}
