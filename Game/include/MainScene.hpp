#ifndef _MAIN_SCENE_HPP_
#define _MAIN_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Memory/RefPtr.h>
#include <Events/MouseEvent.h>
#include <Events/KeyboardEvent.h>
#include <Debug/SystemMetrics.hpp>
#include "World/ChunkManager.hpp"
#include "World/Block.hpp"

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

    Sleak::RefPtr<Sleak::Material> m_blockMaterial;
    ChunkManager m_chunkManager;
    BlockType m_selectedBlock = BlockType::Grass;
    bool m_multithreadedLoading = true;

    // UI state
    bool m_showUI = true;
    bool m_showColliders = false;
    Sleak::SystemMetricsData m_cachedMetrics;
    float m_metricTimer = 0.0f;
};

#endif
