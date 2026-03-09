#include "Game.hpp"
#include "MainMenuScene.hpp"
#include "MainScene.hpp"

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
}

void Game::Loop(float DeltaTime) {
}

void Game::StartWorld(const std::string& savePath, const std::string& worldName,
                      uint32_t seed, bool isNew) {
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
