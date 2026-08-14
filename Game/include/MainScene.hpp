#ifndef _MAIN_SCENE_HPP_
#define _MAIN_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Runtime/Texture.hpp>
#include <Memory/RefPtr.hpp>
#include <Events/MouseEvent.hpp>
#include <Events/KeyboardEvent.hpp>
#include <Events/ApplicationEvent.hpp>
#include <array>
#include <Debug/SystemMetrics.hpp>
#include "World/ChunkManager.hpp"
#include "World/Block.hpp"
#include "World/SaveManager.hpp"
#include "World/BlockEffects.hpp"
#include "World/WorldPersistence.hpp"
#include "Player/PlayerController.hpp"
#include "Player/BlockInteraction.hpp"

namespace Sleak { class Material; class DirectionalLight; }

class MainScene : public Sleak::Scene {
public:
    MainScene(const std::string& name, const std::string& savePath,
              const std::string& worldName, uint32_t seed, bool isNewWorld);
    ~MainScene() override;

    bool Initialize() override;
    void Update(float deltaTime) override;
    void OnDeactivate() override;

    // Save current world state (called by Game when returning to menu)
    void SaveGame();
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
    void SetSaveMessage(const std::string& message, float timer) {
        m_saveMessage = message;
        m_saveMessageTimer = timer;
    }
    float GetGameTime() const { return m_gameTime; }

private:
    void SetupMaterial();
    void SetupSkybox();
    void SetupLighting();
    void RenderUI();

    void OnMousePressed(const Sleak::Events::Input::MouseButtonPressedEvent& e);
    void OnMouseScrolled(const Sleak::Events::Input::MouseScrolledEvent& e);
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);
    void OnKeyReleased(const Sleak::Events::Input::KeyReleasedEvent& e);
    void OnWindowClose(const Sleak::Events::WindowCloseEvent& e);
    void RenderHotbar();

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
    std::array<uint64_t, BlockInteraction::HOTBAR_SLOTS> m_hotbarTextures =
        {};
    bool m_hotbarTexturesLoaded = false;
    bool m_multithreadedLoading = true;
    bool m_vsync = false;
    bool m_frustumCulling = true;
    bool m_occlusionCulling = true;

    // UI state
    bool m_showUI = true;
    bool m_showCrosshair = true;
    bool m_showColliders = false;
    Sleak::SystemMetricsData m_cachedMetrics;
    float m_metricTimer = 0.0f;

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

    // Lighting state (live-editable via settings panel)
    Sleak::DirectionalLight* m_sun = nullptr;
    float m_sunElevation  = 65.0f;   // degrees above horizon (0=sunrise, 90=noon)
    float m_sunAzimuth    = 255.0f;  // degrees clockwise from north
    float m_sunIntensity  = 0.69f;
    float m_sunColorR     = 1.00f;
    float m_sunColorG     = 0.96f;
    float m_sunColorB     = 0.88f;
    float m_ambientIntensity = 0.725f;
    float m_ambientColorR    = 0.45f;
    float m_ambientColorG    = 0.62f;
    float m_ambientColorB    = 1.00f;

    // Texture quality state
    Sleak::TextureFilter m_texFilter = Sleak::TextureFilter::Nearest;
    float m_texLodBias = 0.0f;

    // Fog state (live-editable). Defaults align with LightManager defaults so
    // toggling the fog UI without touching sliders matches the engine's idle
    // state. Horizon RGB is overridden by SetupLighting() to a slightly
    // bluer tint that matches the skybox.
    bool  m_fogEnabled        = true;
    float m_fogHorizonR       = 0.62f;
    float m_fogHorizonG       = 0.78f;
    float m_fogHorizonB       = 1.00f;
    float m_fogZenithR        = 0.42f;
    float m_fogZenithG        = 0.58f;
    float m_fogZenithB        = 0.86f;
    bool  m_heightFogEnabled  = true;
    float m_heightFogTop      = 64.0f;
    float m_heightFogDensity  = 0.25f;
    float m_heightFogFalloff  = 0.05f;
};

#endif
