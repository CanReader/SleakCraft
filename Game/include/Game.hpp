#ifndef _GAME_H_
#define _GAME_H_

#include <GameBase.hpp>
#include <Core/OSDef.hpp>
#include <string>
#include <cstdint>

class MainMenuScene;
class MainScene;

class SLEAK_API Game : public Sleak::GameBase {
public:
  Game();
  Game(Game &&) = delete;
  Game(const Game &) = default;
  ~Game();

  Game &operator=(Game &&) = delete;
  Game &operator=(const Game &) = delete;

  bool Initialize() override;
  void Begin() override;
  void Loop(float DeltaTime) override;

  void StartWorld(const std::string& savePath, const std::string& worldName,
                  uint32_t seed, bool isNew);
  void ReturnToMenu();

  inline bool GetIsGameRunning() { return bIsGameRunning; }

private:
  bool bIsGameRunning = true;
  MainMenuScene* m_menuScene = nullptr;
  MainScene* m_gameScene = nullptr;
};

#endif
