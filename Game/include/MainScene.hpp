#ifndef _MAIN_SCENE_HPP_
#define _MAIN_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Memory/RefPtr.h>
#include <Events/MouseEvent.h>
#include <Events/KeyboardEvent.h>
#include <Debug/SystemMetrics.hpp>
#include "World/ChunkManager.hpp"
#include "World/Block.hpp"
#include "World/SaveManager.hpp"

namespace Sleak { class Material; }

class MainScene : public Sleak::Scene {
public:
    explicit MainScene(const std::string& name);
    ~MainScene() override = default;

    bool Initialize() override;
    void Update(float deltaTime) override;

private:
    void SetupMaterial();
    void SetupSkybox();
    void SetupLighting();
    void RenderUI();

    void OnMousePressed(const Sleak::Events::Input::MouseButtonPressedEvent& e);
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);

    void SaveGame();
    void LoadGame();

    Sleak::RefPtr<Sleak::Material> m_blockMaterial;
    ChunkManager m_chunkManager;
    SaveManager m_saveManager;
    BlockType m_selectedBlock = BlockType::Grass;
    bool m_multithreadedLoading = true;

    // UI state
    bool m_showUI = true;
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
