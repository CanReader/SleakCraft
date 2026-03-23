#include "Game.hpp"
#include "MainMenuScene.hpp"
#include "MainScene.hpp"
#include <Core/Application.hpp>
#include <Core/CommandLine.hpp>
#include "World/SaveManager.hpp"
#include <random>

Game::Game() {
  bIsGameRunning = true;
}

Game::~Game() {
  SLEAK_LOG("The game has been destroyed. Cleaning up resources...");
}

bool Game::Initialize() {
    m_menuScene = new MainMenuScene("MainMenuScene");
    AddScene(m_menuScene);
    SetActiveScene(m_menuScene);
    return true;
}

void Game::Begin() {
    // If -world <name> was passed on the command line, jump straight into that
    // world instead of showing the main menu.
    const std::string worldName = Sleak::CommandLine::GetValue("-world");
    if (worldName.empty()) return;

    const std::string savePath = "saves/" + worldName;

    // Check whether a save already exists for this world name
    SaveManager probe;
    probe.SetSavePath(savePath);
    bool isNew = !probe.HasSave();

    uint32_t seed = 0;
    if (isNew) {
        const std::string seedStr = Sleak::CommandLine::GetValue("-seed");
        seed = (!seedStr.empty()) ? static_cast<uint32_t>(std::stoul(seedStr))
                                  : std::random_device{}();
        SLEAK_INFO("CLI: Creating new world '{}' with seed {}", worldName, seed);
    } else {
        SLEAK_INFO("CLI: Loading existing world '{}'", worldName);
    }

    StartWorld(savePath, worldName, seed, isNew);
}

void Game::Loop(float DeltaTime) {
}

void Game::StartWorld(const std::string& savePath, const std::string& worldName,
                      uint32_t seed, bool isNew) {
    // Wait for GPU before destroying old scene resources
    auto* app = Sleak::Application::GetInstance();
    if (app) app->WaitGPUIdle();

    // Remove old game scene if it exists
    if (m_gameScene) {
        RemoveScene(m_gameScene);
        m_gameScene = nullptr;
    }

    m_gameScene = new MainScene("MainScene", savePath, worldName, seed, isNew);
    AddScene(m_gameScene);
    SetActiveScene(m_gameScene);
}

void Game::ReturnToMenu() {
    auto* app = Sleak::Application::GetInstance();

    // Stop benchmark recording if active
    if (app && app->GetBenchmark() && app->GetBenchmark()->IsRecording())
        app->GetBenchmark()->ToggleRecording();

    // Wait for GPU before destroying scene resources
    if (app) app->WaitGPUIdle();

    // Save the game before returning
    if (m_gameScene) {
        m_gameScene->SaveGame();
        RemoveScene(m_gameScene);
        m_gameScene = nullptr;
    }

    // Re-scan saves and activate menu
    if (m_menuScene) {
        SetActiveScene(m_menuScene);
    }
}
