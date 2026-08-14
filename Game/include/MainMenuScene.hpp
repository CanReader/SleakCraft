#ifndef _MAIN_MENU_SCENE_HPP_
#define _MAIN_MENU_SCENE_HPP_

#include <Core/Scene.hpp>
#include <Events/KeyboardEvent.hpp>
#include <string>
#include <vector>
#include <cstdint>

/// Title screen: world create/load/delete flows and the deferred handoff
/// into Game::StartWorld once a loading screen has had a frame to render.
class MainMenuScene : public Sleak::Scene {
public:
    explicit MainMenuScene(const std::string& name);
    ~MainMenuScene() override;

    bool Initialize() override;
    /// Advances a pending world start, otherwise renders the current panel.
    void Update(float deltaTime) override;
    void OnActivate() override;
    void OnDeactivate() override;

private:
    /// Which panel Update renders this frame.
    enum class MenuState {
        Main,
        CreateWorld,
        LoadWorld,
        Loading
    };

    /// Draws the fullscreen background image behind every menu panel.
    void RenderBackground();
    /// Draws the title panel with Create/Load/Quit buttons.
    void RenderMainMenu();
    /// Draws the new-world form (name, seed) and validates it on submit.
    void RenderCreateWorld();
    /// Draws the scrollable save list with per-entry load/delete controls.
    void RenderLoadWorld();
    /// Draws the loading-screen progress bar while a world start is pending.
    void RenderLoading();

    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);

    /// Rebuilds m_worldList from the saves directory, newest first.
    void ScanSaveDirectory();
    /// Validates the create-world form and queues a deferred new-world start.
    void StartNewWorld();
    /// Queues a deferred start for an existing world from the load list.
    void StartLoadWorld(int index);
    /// Removes a save's files from disk and refreshes the world list.
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
