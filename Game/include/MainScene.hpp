#ifndef _MAIN_SCENE_HPP_
#define _MAIN_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Runtime/Texture.hpp>
#include <Memory/RefPtr.h>
#include <Events/MouseEvent.h>
#include <Events/KeyboardEvent.h>
#include <array>
#include <Debug/SystemMetrics.hpp>
#include "World/ChunkManager.hpp"
#include "World/Block.hpp"
#include "World/SaveManager.hpp"
#include "World/BlockEffects.hpp"

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
    bool HasUnsavedChanges() const;

private:
    void SetupMaterial();
    void SetupSkybox();
    void SetupLighting();
    void RenderUI();

    void OnMousePressed(const Sleak::Events::Input::MouseButtonPressedEvent& e);
    void OnMouseScrolled(const Sleak::Events::Input::MouseScrolledEvent& e);
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);
    void OnKeyReleased(const Sleak::Events::Input::KeyReleasedEvent& e);
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
    BlockType m_selectedBlock = BlockType::Grass;
    int m_selectedSlot = 0;
    static constexpr int HOTBAR_SLOTS = 9;
    std::array<BlockType, HOTBAR_SLOTS> m_hotbar = {{
        BlockType::Grass, BlockType::Dirt, BlockType::Stone,
        BlockType::Cobblestone, BlockType::OakLog, BlockType::DarkOakLog,
        BlockType::SpruceLog, BlockType::OakPlanks, BlockType::Bricks
    }};
    std::array<uint64_t, HOTBAR_SLOTS> m_hotbarTextures = {};
    bool m_hotbarTexturesLoaded = false;
    bool m_multithreadedLoading = true;
    bool m_vsync = false;

    // UI state
    bool m_showUI = true;
    bool m_showCrosshair = true;
    bool m_showColliders = false;
    Sleak::SystemMetricsData m_cachedMetrics;
    float m_metricTimer = 0.0f;

    // Save/load UI feedback
    float m_saveMessageTimer = 0.0f;
    std::string m_saveMessage;

    // Auto-save
    float m_autoSaveTimer = 0.0f;
    static constexpr float AUTO_SAVE_INTERVAL = 120.0f;

    // Minecraft-style double-tap space to toggle fly
    bool m_flying = false;
    float m_lastSpacePressTime = -1.0f;
    float m_gameTime = 0.0f;
    float m_flySpeed = 6.0f;
    float m_flySprintMultiplier = 2.5f;
    static constexpr float DOUBLE_TAP_WINDOW = 0.3f;
    bool m_spaceHeld = false;
    bool m_shiftHeld = false;
    bool m_ctrlHeld = false;

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
    Sleak::TextureFilter m_texFilter = Sleak::TextureFilter::Anisotropic16x;
    float m_texLodBias = 0.0f;
};

#endif
