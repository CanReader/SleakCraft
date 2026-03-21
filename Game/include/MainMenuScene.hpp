#ifndef _MAIN_MENU_SCENE_HPP_
#define _MAIN_MENU_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Events/KeyboardEvent.h>
#include <string>
#include <vector>
#include <cstdint>

class MainMenuScene : public Sleak::Scene {
public:
    explicit MainMenuScene(const std::string& name);
    ~MainMenuScene() override;

    bool Initialize() override;
    void Update(float deltaTime) override;
    void OnActivate() override;
    void OnDeactivate() override;

private:
    enum class MenuState {
        Main,
        CreateWorld,
        LoadWorld,
        Loading
    };

    void RenderMainMenu();
    void RenderCreateWorld();
    void RenderLoadWorld();
    void RenderLoading();

    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);

    void ScanSaveDirectory();
    void StartNewWorld();
    void StartLoadWorld(int index);
    void DeleteWorld(int index);

    MenuState m_menuState = MenuState::Main;

    // World list
    struct WorldEntry {
        std::string name;
        std::string path;
        int64_t lastPlayed = 0;
        uint32_t seed = 0;
    };
    std::vector<WorldEntry> m_worldList;
    int m_selectedWorld = -1;

    // New world form
    std::string m_newWorldName;
    std::string m_newWorldSeed;
    std::string m_errorMessage;

    // Loading state
    std::string m_loadingWorldName;
    float m_loadingProgress = 0.0f;
    float m_loadingTimer = 0.0f;
    bool m_loadingStarted = false;

    // Pending world to start (deferred to next frame to allow loading screen to render)
    bool m_pendingStart = false;
    std::string m_pendingSavePath;
    std::string m_pendingWorldName;
    uint32_t m_pendingSeed = 0;
    bool m_pendingIsNew = true;

    // Delete confirmation
    int m_deleteConfirmIndex = -1;

    std::string m_keyPressedHandlerId;
};

#endif
