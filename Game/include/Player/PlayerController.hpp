#ifndef _PLAYER_CONTROLLER_HPP_
#define _PLAYER_CONTROLLER_HPP_

#include <Events/KeyboardEvent.hpp>

class ChunkManager;
class MainScene;

// Fly toggle (double-tap space), held movement-key state, and the per-frame
// FirstPersonController/RigidbodyComponent <-> ChunkManager collision glue.
// Camera/FPC/rigidbody are fetched fresh each call via the owning MainScene.
class PlayerController {
public:
    PlayerController(ChunkManager& chunkManager, MainScene& scene)
        : m_chunkManager(chunkManager), m_scene(scene) {}

    void ApplyTuning();
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);
    void OnKeyReleased(const Sleak::Events::Input::KeyReleasedEvent& e);
    void Update();

private:
    ChunkManager& m_chunkManager;
    MainScene& m_scene;

    // Minecraft-style double-tap space to toggle fly
    bool m_flying = false;
    float m_lastSpacePressTime = -1.0f;
    float m_flySpeed = 10.0f;
    float m_flySprintMultiplier = 2.5f;
    static constexpr float DOUBLE_TAP_WINDOW = 0.3f;
    bool m_spaceHeld = false;
    bool m_shiftHeld = false;
    bool m_ctrlHeld = false;
};

#endif
