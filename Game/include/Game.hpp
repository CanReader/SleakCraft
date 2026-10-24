#ifndef _GAME_H_
#define _GAME_H_

#include <Core/GameBase.hpp>
#include <Core/OSDef.hpp>
#include <string>
#include <cstdint>
#include <optional>

#include "Player/CameraPose.hpp"

class MainMenuScene;
class MainScene;

/// Engine entry point for SleakCraft. Owns the menu and in-world scenes and
/// switches the active Scene between them.
/// @ingroup app
class SLEAK_API Game : public Sleak::GameBase {
public:
  Game();
  Game(Game &&) = delete;
  Game(const Game &) = default;
  ~Game();

  Game &operator=(Game &&) = delete;
  Game &operator=(const Game &) = delete;

  /// Creates and activates the main menu scene.
  bool Initialize() override;
  /// Debug-only: if -world was passed on the command line, skips the menu
  /// and starts that world directly.
  void Begin() override;
  void Loop(float DeltaTime) override;

  /// Tears down any active game scene and activates a fresh MainScene for
  /// the given save.
  void StartWorld(const std::string& savePath, const std::string& worldName,
                  uint32_t seed, bool isNew);
  /// Saves the active world if dirty, destroys it, and reactivates the menu.
  void ReturnToMenu();

  inline bool GetIsGameRunning() { return bIsGameRunning; }

  /// Autopilot speed in m/s that new game scenes fly at, 0 for none.
  void SetAutoFlySpeed(float speed) { m_autoFlySpeed = speed; }
  /// Autopilot speed in m/s set by SetAutoFlySpeed().
  float GetAutoFlySpeed() const { return m_autoFlySpeed; }
  /// Pins the camera of new game scenes at a fixed pose.
  void SetCameraPose(const CameraPose& pose) { m_cameraPose = pose; }
  /// Pose set by SetCameraPose(), if any.
  const std::optional<CameraPose>& GetCameraPose() const {
      return m_cameraPose;
  }

private:
  bool bIsGameRunning = true;
  float m_autoFlySpeed = 0.0f;
  std::optional<CameraPose> m_cameraPose;
  MainMenuScene* m_menuScene = nullptr;
  MainScene* m_gameScene = nullptr;
};

#endif
