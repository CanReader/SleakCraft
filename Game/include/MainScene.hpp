#ifndef _MAIN_SCENE_HPP_
#define _MAIN_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Memory/RefPtr.h>
#include "World/ChunkManager.hpp"

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

    static constexpr float PLAYER_EYE_HEIGHT = 6.62f;

    Sleak::RefPtr<Sleak::Material> m_blockMaterial;
    ChunkManager m_chunkManager;
};

#endif
