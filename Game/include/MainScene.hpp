#ifndef _MAIN_SCENE_HPP_
#define _MAIN_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Runtime/Texture.hpp>
#include <Memory/RefPtr.hpp>
#include <Events/MouseEvent.hpp>
#include <Events/KeyboardEvent.hpp>
#include <Events/ApplicationEvent.hpp>
#include "World/ChunkManager.hpp"
#include "World/Block.hpp"
#include "World/SaveManager.hpp"
#include "World/BlockEffects.hpp"
#include "World/WorldPersistence.hpp"
#include "Player/PlayerController.hpp"
#include "Player/BlockInteraction.hpp"
#include "UI/HudPanel.hpp"
#include "UI/SettingsPanel.hpp"
#include "UI/Hotbar.hpp"

namespace Sleak { class Material; class DirectionalLight; }

/// The in-world gameplay scene: bring-up of the block material/skybox/
/// lighting/camera, per-frame orchestration of the chunk/player/UI
/// collaborators, and input event routing. Gameplay and rendering logic
/// itself lives on ChunkManager, PlayerController, BlockInteraction, and the
/// UI panels; this class wires them together and owns their lifetime.
class MainScene : public Sleak::Scene {
public:
    MainScene(const std::string& name, const std::string& savePath,
              const std::string& worldName, uint32_t seed, bool isNewWorld);
    ~MainScene() override;

    /// Sets up the camera, material/skybox/lighting, loads or generates the
    /// world, registers benchmark metrics, and subscribes input handlers.
    bool Initialize() override;
    /// Advances world time, ticks the chunk/player/effects collaborators,
    /// and renders the world, HUD, and hotbar for one frame.
    void Update(float deltaTime) override;
    void OnDeactivate() override;

    // Save current world state (called by Game when returning to menu)
    void SaveGame();
    /// Unregisters this scene's benchmark metric callbacks so the Benchmark
    /// singleton, which outlives the scene, doesn't call into freed state.
    void UnregisterBenchmarkMetrics();
    bool HasUnsavedChanges() const;

    // Accessors for WorldPersistence/PlayerController/BlockInteraction
    // (player/scene state they drive)
    const std::string& GetWorldName() const { return m_worldName; }
    BlockType GetSelectedBlock() const {
        return m_blockInteraction.GetSelectedBlock();
    }
    void SetSelectedBlock(BlockType type) {
        m_blockInteraction.SetSelectedBlock(type);
    }
    bool IsSaveLocked() const { return m_saveLocked; }
    void SetSaveLocked(bool locked) { m_saveLocked = locked; }
    bool IsMultithreadedLoading() const { return m_multithreadedLoading; }
    void SetMultithreadedLoading(bool enabled) {
        m_multithreadedLoading = enabled;
    }
    void SetSaveMessage(const std::string& message, float timer) {
        m_saveMessage = message;
        m_saveMessageTimer = timer;
    }
    float GetSaveMessageTimer() const { return m_saveMessageTimer; }
    const std::string& GetSaveMessage() const { return m_saveMessage; }
    float GetGameTime() const { return m_gameTime; }

    // Accessors for HudPanel/SettingsPanel (toggles that engine setup code
    // in MainScene also reads at Initialize time)
    bool IsShowCrosshair() const { return m_showCrosshair; }
    void SetShowCrosshair(bool show) { m_showCrosshair = show; }
    bool IsVSyncEnabled() const { return m_vsync; }
    void SetVSyncEnabled(bool enabled) { m_vsync = enabled; }
    bool IsFrustumCullingEnabled() const { return m_frustumCulling; }
    void SetFrustumCullingEnabled(bool enabled) { m_frustumCulling = enabled; }
    bool IsOcclusionCullingEnabled() const { return m_occlusionCulling; }
    void SetOcclusionCullingEnabled(bool enabled) {
        m_occlusionCulling = enabled;
    }
    Sleak::RefPtr<Sleak::Material> GetBlockMaterial() const {
        return m_blockMaterial;
    }

private:
    /// Builds the block/water materials, including the runtime texture
    /// atlas, and applies the settings panel's texture filter.
    void SetupMaterial();
    void SetupSkybox();
    /// Creates the sun light and applies the voxel-tuned GraphicsConfig
    /// (shadows on, SSAO/SSR/IBL/bloom off) plus fog and culling settings.
    void SetupLighting();
    void RenderUI();

    void OnMousePressed(const Sleak::Events::Input::MouseButtonPressedEvent& e);
    void OnMouseScrolled(const Sleak::Events::Input::MouseScrolledEvent& e);
    /// Forwards to the player/block-interaction collaborators, then handles
    /// scene-level keys (ESC to menu, F3 UI toggle, F5 save, F6 reload).
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);
    void OnKeyReleased(const Sleak::Events::Input::KeyReleasedEvent& e);
    /// Persists dirty chunks before the renderer tears down.
    void OnWindowClose(const Sleak::Events::WindowCloseEvent& e);

    void LoadGame();

    std::string m_savePath;
    std::string m_worldName;
    uint32_t m_worldSeed;
    bool m_isNewWorld;

    Sleak::RefPtr<Sleak::Material> m_blockMaterial;
    Sleak::RefPtr<Sleak::Material> m_waterMaterial;
    ChunkManager m_chunkManager;
    BlockEffects m_blockEffects;
    SaveManager m_saveManager;
    WorldPersistence m_worldPersistence;
    PlayerController m_playerController;
    BlockInteraction m_blockInteraction;
    HudPanel m_hudPanel;
    SettingsPanel m_settingsPanel;
    Hotbar m_hotbar;
    bool m_multithreadedLoading = true;
    bool m_vsync = false;
    bool m_frustumCulling = true;
    bool m_occlusionCulling = true;

    // UI state
    bool m_showUI = true;
    bool m_showCrosshair = true;
    bool m_showColliders = false;

    // Save/load UI feedback
    float m_saveMessageTimer = 0.0f;
    std::string m_saveMessage;
    // Never save over a save that failed to load
    bool m_saveLocked = false;
    float m_loadConfirmTimer = 0.0f;

    // Auto-save
    float m_autoSaveTimer = 0.0f;
    static constexpr float AUTO_SAVE_INTERVAL = 120.0f;

    float m_gameTime = 0.0f;

    std::string m_windowCloseHandlerId;
    std::string m_mousePressedHandlerId;
    std::string m_mouseScrolledHandlerId;
    std::string m_keyPressedHandlerId;
    std::string m_keyReleasedHandlerId;

    // Sun light created here (needs Scene::AddObject/shadow config); live
    // elevation/azimuth/color/intensity edits live on SettingsPanel.
    Sleak::DirectionalLight* m_sun = nullptr;
};

#endif
