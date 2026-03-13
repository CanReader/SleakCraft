#ifndef _MAIN_SCENE_HPP_
#define _MAIN_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Memory/RefPtr.h>
#include <Events/MouseEvent.h>
#include <Events/KeyboardEvent.h>
#include <array>
#include <Debug/SystemMetrics.hpp>
#include "World/ChunkManager.hpp"
#include "World/Block.hpp"
#include "World/SaveManager.hpp"

namespace Sleak { class Material; }

class MainScene : public Sleak::Scene {
public:
    MainScene(const std::string& name, const std::string& savePath,
              const std::string& worldName, uint32_t seed, bool isNewWorld);
    ~MainScene() override = default;

    bool Initialize() override;
    void Update(float deltaTime) override;

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
    void RenderHotbar();

    void LoadGame();

    std::string m_savePath;
    std::string m_worldName;
    uint32_t m_worldSeed;
    bool m_isNewWorld;

    Sleak::RefPtr<Sleak::Material> m_blockMaterial;
    ChunkManager m_chunkManager;
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
};

#endif
