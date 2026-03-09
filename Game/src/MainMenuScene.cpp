#include "MainMenuScene.hpp"
#include "Game.hpp"
#include <Core/Application.hpp>
#include <Camera/Camera.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Events/KeyboardEvent.h>
#include <UI/UI.hpp>
#include "World/SaveManager.hpp"
#include <ctime>
#include <cstdlib>
#include <algorithm>

using namespace Sleak;
using namespace Sleak::UI;

MainMenuScene::MainMenuScene(const std::string& name)
    : Scene(name) {}

bool MainMenuScene::Initialize() {
    Scene::Initialize();

    // Disable camera controller in menu (Scene::Initialize creates one by default)
    auto* cam = GetDebugCamera();
    if (cam) {
        auto* fpc = cam->GetComponent<FirstPersonController>();
        if (fpc) fpc->SetEnabled(false);
    }

    EventDispatcher::RegisterEventHandler(this, &MainMenuScene::OnKeyPressed);

    ScanSaveDirectory();
    return true;
}

void MainMenuScene::OnActivate() {
    Scene::OnActivate();

    // Disable camera controller so cursor is visible
    auto* cam = GetDebugCamera();
    if (cam) {
        auto* fpc = cam->GetComponent<FirstPersonController>();
        if (fpc) fpc->SetEnabled(false);
    }

    m_menuState = MenuState::Main;
    m_errorMessage.clear();
    m_deleteConfirmIndex = -1;
    ScanSaveDirectory();
}

void MainMenuScene::OnKeyPressed(const Events::Input::KeyPressedEvent& e) {
    if_key_press(KEY__ESCAPE) {
        switch (m_menuState) {
            case MenuState::Main: {
                auto* app = Application::GetInstance();
                if (app) app->CloseApplication();
                break;
            }
            case MenuState::CreateWorld:
            case MenuState::LoadWorld:
                m_menuState = MenuState::Main;
                m_errorMessage.clear();
                m_deleteConfirmIndex = -1;
                break;
            case MenuState::Loading:
                break; // Can't cancel loading
        }
    }
}

void MainMenuScene::ScanSaveDirectory() {
    m_worldList.clear();
    auto dirs = SaveManager::ListSaveDirectories("saves");

    for (auto& dir : dirs) {
        WorldMeta meta;
        if (SaveManager::ReadWorldMetaOnly(dir, meta)) {
            WorldEntry entry;
            entry.name = meta.worldName;
            entry.path = dir;
            entry.lastPlayed = meta.saveTimestamp;
            entry.seed = meta.seed;
            m_worldList.push_back(entry);
        }
    }

    // Sort by last played (most recent first)
    std::sort(m_worldList.begin(), m_worldList.end(),
        [](const WorldEntry& a, const WorldEntry& b) {
            return a.lastPlayed > b.lastPlayed;
        });

    m_selectedWorld = -1;
}

void MainMenuScene::Update(float deltaTime) {
    Scene::Update(deltaTime);

    // Handle deferred world start (allows loading screen to render)
    if (m_pendingStart) {
        m_loadingTimer += deltaTime;
        m_loadingProgress = std::min(m_loadingTimer / 0.5f, 0.9f);

        if (m_loadingTimer >= 0.3f) {
            m_pendingStart = false;
            auto* app = Application::GetInstance();
            if (app) {
                auto* game = static_cast<Game*>(app->GetGame());
                if (game)
                    game->StartWorld(m_pendingSavePath, m_pendingWorldName,
                                     m_pendingSeed, m_pendingIsNew);
            }
            return;
        }
    }

    switch (m_menuState) {
        case MenuState::Main:       RenderMainMenu(); break;
        case MenuState::CreateWorld: RenderCreateWorld(); break;
        case MenuState::LoadWorld:   RenderLoadWorld(); break;
        case MenuState::Loading:     RenderLoading(); break;
    }
}

void MainMenuScene::RenderMainMenu() {
    float vw = GetViewportWidth();
    float vh = GetViewportHeight();
    float panelW = 400.0f;

    // Use a fullscreen invisible window to center content
    SetNextWindowPos(0, 0, true);
    SetNextWindowSize(vw, vh, true);
    PushStyleVarVec(StyleVar_WindowPadding, 0.0f, 0.0f);
    BeginPanel("##MenuBG", 0, 0, 0.0f,
               PanelFlags_NoTitleBar | PanelFlags_NoMove);
    PopStyleVar(1);

    // Calculate total content height to center vertically
    float logoW = 0, logoH = 0;
    uint64_t logoTex = LoadTextureForUI("assets/textures/Logo.png", &logoW, &logoH);
    float drawW = 0, drawH = 0;
    if (logoTex != 0 && logoW > 0) {
        float logoScale = panelW / logoW;
        drawW = logoW * logoScale;
        drawH = logoH * logoScale;
    }

    float btnH = 40.0f;
    float btnSpacing = 8.0f;
    float buttonsH = btnH * 3 + btnSpacing * 2 + 16.0f + 20.0f; // 3 buttons + spacing + padding + version
    float gap = 24.0f;
    float totalH = drawH + gap + buttonsH;

    // Start Y to center everything
    float startY = (vh - totalH) * 0.5f;
    SetCursorPosY(startY);

    // Logo centered horizontally
    if (logoTex != 0) {
        SetCursorPosX((vw - drawW) * 0.5f);
        Image(logoTex, drawW, drawH);
        Dummy(0, gap);
    }

    // Buttons panel (child window with background)
    float btnW = panelW - 48.0f;
    float childX = (vw - panelW) * 0.5f;
    SetCursorPosX(childX);

    PushStyleVar(StyleVar_WindowRounding, 8.0f);
    PushStyleVarVec(StyleVar_WindowPadding, 24.0f, 16.0f);
    PushStyleColor(StyleColor_ChildBg, 0.1f, 0.1f, 0.12f, 0.85f);

    BeginChildSized("##Buttons", panelW);

    Dummy(0, 8);

    // Create a World
    SetCursorPosX(24.0f);
    PushStyleColor(StyleColor_Button, 0.2f, 0.5f, 0.2f, 1.0f);
    PushStyleColor(StyleColor_ButtonHovered, 0.3f, 0.65f, 0.3f, 1.0f);
    PushStyleColor(StyleColor_ButtonActive, 0.15f, 0.4f, 0.15f, 1.0f);
    if (ButtonSized("Create a World", btnW, btnH)) {
        m_menuState = MenuState::CreateWorld;
        m_newWorldName.clear();
        m_newWorldSeed.clear();
        m_errorMessage.clear();
    }
    PopStyleColor(3);

    Dummy(0, btnSpacing);

    // Load a World
    SetCursorPosX(24.0f);
    PushStyleColor(StyleColor_Button, 0.2f, 0.35f, 0.55f, 1.0f);
    PushStyleColor(StyleColor_ButtonHovered, 0.3f, 0.45f, 0.65f, 1.0f);
    PushStyleColor(StyleColor_ButtonActive, 0.15f, 0.25f, 0.45f, 1.0f);
    if (ButtonSized("Load a World", btnW, btnH)) {
        m_menuState = MenuState::LoadWorld;
        m_deleteConfirmIndex = -1;
        ScanSaveDirectory();
    }
    PopStyleColor(3);

    Dummy(0, btnSpacing);

    // Exit
    SetCursorPosX(24.0f);
    PushStyleColor(StyleColor_Button, 0.55f, 0.15f, 0.15f, 1.0f);
    PushStyleColor(StyleColor_ButtonHovered, 0.7f, 0.2f, 0.2f, 1.0f);
    PushStyleColor(StyleColor_ButtonActive, 0.4f, 0.1f, 0.1f, 1.0f);
    if (ButtonSized("Exit", btnW, btnH)) {
        auto* app = Application::GetInstance();
        if (app) app->CloseApplication();
    }
    PopStyleColor(3);

    Dummy(0, 8);
    TextDisabled("SleakCraft v0.1");

    EndChild();
    PopStyleColor(1); // ChildBg
    PopStyleVar(2);   // WindowRounding, WindowPadding

    EndPanel();
}

void MainMenuScene::RenderCreateWorld() {
    float vw = GetViewportWidth();
    float vh = GetViewportHeight();
    float panelW = 400.0f;

    SetNextWindowPos(vw * 0.5f - panelW * 0.5f, vh * 0.5f - 160.0f, true);
    SetNextWindowSize(panelW, 0, true);

    PushStyleVar(StyleVar_WindowRounding, 8.0f);
    PushStyleVarVec(StyleVar_WindowPadding, 24.0f, 24.0f);

    BeginPanel("CreateWorld", 0, 0, 0.85f,
               PanelFlags_NoTitleBar | PanelFlags_AutoResize | PanelFlags_NoMove);

    Dummy(0, 4);
    Text("Create a New World");
    Separator();
    Dummy(0, 8);

    // World Name input
    Text("World Name:");
    SetNextItemWidth(panelW - 48.0f);
    InputTextString("##worldname", &m_newWorldName);

    Dummy(0, 4);

    // Seed input
    Text("World Seed (optional):");
    SetNextItemWidth(panelW - 48.0f);
    InputTextString("##worldseed", &m_newWorldSeed);
    TextDisabled("Leave empty for random seed");

    // Error message
    if (!m_errorMessage.empty()) {
        Dummy(0, 4);
        TextColored(1.0f, 0.3f, 0.3f, 1.0f, "%s", m_errorMessage.c_str());
    }

    Dummy(0, 12);

    float btnW = (panelW - 48.0f - 8.0f) * 0.5f;

    // Create button
    PushStyleColor(StyleColor_Button, 0.2f, 0.5f, 0.2f, 1.0f);
    PushStyleColor(StyleColor_ButtonHovered, 0.3f, 0.65f, 0.3f, 1.0f);
    PushStyleColor(StyleColor_ButtonActive, 0.15f, 0.4f, 0.15f, 1.0f);
    if (ButtonSized("Create", btnW, 36.0f))
        StartNewWorld();
    PopStyleColor(3);

    SameLine();

    // Back button
    if (ButtonSized("Back", btnW, 36.0f))
        m_menuState = MenuState::Main;

    Dummy(0, 4);
    EndPanel();
    PopStyleVar(2);
}

void MainMenuScene::RenderLoadWorld() {
    float vw = GetViewportWidth();
    float vh = GetViewportHeight();
    float panelW = 450.0f;

    SetNextWindowPos(vw * 0.5f - panelW * 0.5f, vh * 0.5f - 200.0f, true);
    SetNextWindowSize(panelW, 0, true);

    PushStyleVar(StyleVar_WindowRounding, 8.0f);
    PushStyleVarVec(StyleVar_WindowPadding, 24.0f, 24.0f);

    BeginPanel("LoadWorld", 0, 0, 0.85f,
               PanelFlags_NoTitleBar | PanelFlags_AutoResize | PanelFlags_NoMove);

    Dummy(0, 4);
    Text("Load a World");
    Separator();
    Dummy(0, 8);

    if (m_worldList.empty()) {
        TextDisabled("No saved worlds found.");
        Dummy(0, 8);
    } else {
        float listH = std::min(static_cast<float>(m_worldList.size()) * 60.0f + 8.0f, 300.0f);
        if (BeginListBox("##worldlist", panelW - 48.0f, listH)) {
            for (int i = 0; i < static_cast<int>(m_worldList.size()); ++i) {
                auto& entry = m_worldList[i];
                bool selected = (m_selectedWorld == i);

                // Format the entry
                char label[256];
                if (entry.lastPlayed > 0) {
                    time_t t = static_cast<time_t>(entry.lastPlayed);
                    struct tm tm_buf;
#ifdef _WIN32
                    localtime_s(&tm_buf, &t);
#else
                    localtime_r(&t, &tm_buf);
#endif
                    char timeBuf[64];
                    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", &tm_buf);
                    snprintf(label, sizeof(label), "%s\n  Seed: %u | Last Played: %s",
                             entry.name.c_str(), entry.seed, timeBuf);
                } else {
                    snprintf(label, sizeof(label), "%s\n  Seed: %u",
                             entry.name.c_str(), entry.seed);
                }

                if (Selectable(label, selected))
                    m_selectedWorld = i;
            }
            EndListBox();
        }

        Dummy(0, 4);

        // Delete confirmation
        if (m_deleteConfirmIndex >= 0 && m_deleteConfirmIndex < static_cast<int>(m_worldList.size())) {
            TextColored(1.0f, 0.5f, 0.2f, 1.0f, "Delete '%s'?",
                        m_worldList[m_deleteConfirmIndex].name.c_str());
            PushStyleColor(StyleColor_Button, 0.6f, 0.15f, 0.15f, 1.0f);
            PushStyleColor(StyleColor_ButtonHovered, 0.75f, 0.2f, 0.2f, 1.0f);
            PushStyleColor(StyleColor_ButtonActive, 0.5f, 0.1f, 0.1f, 1.0f);
            if (ButtonSized("Confirm Delete", 130.0f, 30.0f)) {
                DeleteWorld(m_deleteConfirmIndex);
                m_deleteConfirmIndex = -1;
            }
            PopStyleColor(3);
            SameLine();
            if (ButtonSized("Cancel", 80.0f, 30.0f))
                m_deleteConfirmIndex = -1;
            Dummy(0, 4);
        }
    }

    Dummy(0, 4);

    float btnW = (panelW - 48.0f - 16.0f) / 3.0f;

    // Play button
    bool hasSelection = m_selectedWorld >= 0 && m_selectedWorld < static_cast<int>(m_worldList.size());
    PushStyleColor(StyleColor_Button,
                   hasSelection ? 0.2f : 0.3f,
                   hasSelection ? 0.5f : 0.3f,
                   hasSelection ? 0.2f : 0.3f, 1.0f);
    PushStyleColor(StyleColor_ButtonHovered, 0.3f, 0.65f, 0.3f, 1.0f);
    PushStyleColor(StyleColor_ButtonActive, 0.15f, 0.4f, 0.15f, 1.0f);
    if (ButtonSized("Play", btnW, 36.0f) && hasSelection)
        StartLoadWorld(m_selectedWorld);
    PopStyleColor(3);

    SameLine();

    // Delete button
    PushStyleColor(StyleColor_Button,
                   hasSelection ? 0.55f : 0.3f,
                   hasSelection ? 0.15f : 0.3f,
                   hasSelection ? 0.15f : 0.3f, 1.0f);
    PushStyleColor(StyleColor_ButtonHovered, 0.7f, 0.2f, 0.2f, 1.0f);
    PushStyleColor(StyleColor_ButtonActive, 0.4f, 0.1f, 0.1f, 1.0f);
    if (ButtonSized("Delete", btnW, 36.0f) && hasSelection)
        m_deleteConfirmIndex = m_selectedWorld;
    PopStyleColor(3);

    SameLine();

    // Back button
    if (ButtonSized("Back", btnW, 36.0f))
        m_menuState = MenuState::Main;

    Dummy(0, 4);
    EndPanel();
    PopStyleVar(2);
}

void MainMenuScene::RenderLoading() {
    float vw = GetViewportWidth();
    float vh = GetViewportHeight();
    float panelW = 350.0f;

    SetNextWindowPos(vw * 0.5f - panelW * 0.5f, vh * 0.5f - 60.0f, true);
    SetNextWindowSize(panelW, 0, true);

    PushStyleVar(StyleVar_WindowRounding, 8.0f);
    PushStyleVarVec(StyleVar_WindowPadding, 24.0f, 24.0f);

    BeginPanel("Loading", 0, 0, 0.85f,
               PanelFlags_NoTitleBar | PanelFlags_AutoResize | PanelFlags_NoMove);

    Dummy(0, 8);
    Text("Loading: %s", m_loadingWorldName.c_str());
    Dummy(0, 12);

    ProgressBar(m_loadingProgress, panelW - 48.0f, 20.0f);

    Dummy(0, 4);
    TextDisabled("Generating terrain...");
    Dummy(0, 8);

    EndPanel();
    PopStyleVar(2);
}

void MainMenuScene::StartNewWorld() {
    // Validate name
    if (m_newWorldName.empty()) {
        m_errorMessage = "World name cannot be empty!";
        return;
    }

    // Check for invalid characters
    for (char c : m_newWorldName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            m_errorMessage = "World name contains invalid characters!";
            return;
        }
    }

    // Check for duplicate name
    std::string savePath = "saves/" + m_newWorldName;
    for (auto& entry : m_worldList) {
        if (entry.path == savePath) {
            m_errorMessage = "A world with this name already exists!";
            return;
        }
    }

    // Parse seed
    uint32_t seed;
    if (m_newWorldSeed.empty()) {
        seed = static_cast<uint32_t>(time(nullptr)) ^ static_cast<uint32_t>(rand());
    } else {
        // Try to parse as number, otherwise hash the string
        char* endPtr;
        unsigned long val = strtoul(m_newWorldSeed.c_str(), &endPtr, 10);
        if (*endPtr == '\0') {
            seed = static_cast<uint32_t>(val);
        } else {
            // Hash the string
            seed = 0;
            for (char c : m_newWorldSeed)
                seed = seed * 31 + static_cast<uint32_t>(c);
        }
    }

    // Switch to loading state
    m_loadingWorldName = m_newWorldName;
    m_loadingProgress = 0.0f;
    m_loadingTimer = 0.0f;
    m_menuState = MenuState::Loading;

    // Defer actual world creation to next frame
    m_pendingStart = true;
    m_pendingSavePath = savePath;
    m_pendingWorldName = m_newWorldName;
    m_pendingSeed = seed;
    m_pendingIsNew = true;
}

void MainMenuScene::StartLoadWorld(int index) {
    if (index < 0 || index >= static_cast<int>(m_worldList.size()))
        return;

    auto& entry = m_worldList[index];

    m_loadingWorldName = entry.name;
    m_loadingProgress = 0.0f;
    m_loadingTimer = 0.0f;
    m_menuState = MenuState::Loading;

    m_pendingStart = true;
    m_pendingSavePath = entry.path;
    m_pendingWorldName = entry.name;
    m_pendingSeed = entry.seed;
    m_pendingIsNew = false;
}

void MainMenuScene::DeleteWorld(int index) {
    if (index < 0 || index >= static_cast<int>(m_worldList.size()))
        return;

    SaveManager::DeleteSaveDirectory(m_worldList[index].path);
    m_worldList.erase(m_worldList.begin() + index);

    if (m_selectedWorld >= static_cast<int>(m_worldList.size()))
        m_selectedWorld = static_cast<int>(m_worldList.size()) - 1;
}
