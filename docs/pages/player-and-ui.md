# Player and UI {#player-and-ui}

Movement, block interaction, and the three ImGui-backed panels, all owned and wired
together by `MainScene`.

| Type | Role |
| :--- | :--- |
| `PlayerController` | Movement, flight toggle, and per-frame voxel collision against the world. |
| `BlockInteraction` | Raycast targeting, break and place, hotbar selection, outline rendering. |
| `BlockEffects` | Break and place animations; holds a placement until its animation completes. |
| `HudPanel` | Performance and world readouts plus the crosshair. |
| `SettingsPanel` | Render distance, graphics toggles, and the attached sun light. |
| `Hotbar` | The block selection strip, driven entirely by `BlockInteraction`. |
| `TextureAtlas` | Static tile UV lookup used for the block icons the panels draw. |

---

## 1. Movement (`PlayerController`)

```cpp
class PlayerController {
public:
    PlayerController(ChunkManager& chunkManager, MainScene& scene);
    void ApplyTuning();
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);
    void OnKeyReleased(const Sleak::Events::Input::KeyReleasedEvent& e);
    void Update();
};
```

`ApplyTuning()` pushes SleakCraft's movement constants onto the active camera's
`FirstPersonController` component: walk speed, sprint multiplier, acceleration, braking,
ground friction, air control, jump velocity, and separate fly speed/sprint values.
`Update()` runs every frame: while flying, it derives vertical input from held
space/ctrl and zeroes the rigidbody's velocity so gravity does not fight it; either way,
it then calls `VoxelQueries::ResolveVoxelCollision` through `ChunkManager` and applies
the correction directly to the camera position, clearing vertical velocity on landing or
hitting a ceiling. See @ref world-and-chunks for the collision query itself.

Space is edge-triggered for one extra behavior: a double-tap within `0.3s`
(`DOUBLE_TAP_WINDOW`) toggles flight, disabling gravity on the rigidbody and handing
vertical control to `FirstPersonController::SetFlying`.

---

## 2. Block Interaction (`BlockInteraction`)

```cpp
class BlockInteraction {
public:
    static constexpr int HOTBAR_SLOTS = 9;

    BlockInteraction(ChunkManager& chunkManager, BlockEffects& blockEffects,
                      MainScene& scene);
    void OnMousePressed(const Sleak::Events::Input::MouseButtonPressedEvent& e);
    void OnMouseScrolled(const Sleak::Events::Input::MouseScrolledEvent& e);
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);
    void RenderOutline();
    void DrainCompletedPlacements();
};
```

A left click raycasts from the camera through `VoxelQueries::VoxelRaycast` and breaks
the hit block; a right click places the currently selected hotbar block into the empty
cell just before the hit. Placement does not call `ChunkManager::SetBlockAt` directly:
it goes through `BlockEffects::SpawnPlaceEffect`, which plays a scale-in animation, and
the actual voxel edit only lands once `BlockInteraction::DrainCompletedPlacements` pops
it from `BlockEffects::PopCompletedPlacements` after the animation finishes (about
0.15s later). `BlockEffects::DrainAllPlacements` force-completes every in-flight
placement, used on save and scene exit so a placement mid-animation is never lost.

The hotbar itself is nine fixed slots, cycled by scroll wheel or selected directly with
number keys `1`-`9`:

```cpp
std::array<BlockType, HOTBAR_SLOTS> m_hotbar = {
    {BlockType::Grass, BlockType::Dirt, BlockType::Stone,
     BlockType::Cobblestone, BlockType::OakLog, BlockType::DarkOakLog,
     BlockType::SpruceLog, BlockType::OakPlanks, BlockType::Bricks}};
```

---

## 3. UI Panels

Game UI never touches ImGui directly; every panel below is built on
`Sleak::UI::BeginPanel` / `Text` / `EndPanel` and the small widget set in
`Engine/include/public/UI/UI.hpp`:

```cpp
ENGINE_API void BeginPanel(const char* name, float x, float y, float bgAlpha = 0.4f,
                            int flags = PanelFlags_NoTitleBar | PanelFlags_AutoResize |
                                        PanelFlags_NoMove | PanelFlags_NoInput |
                                        PanelFlags_NoFocusOnAppear);
ENGINE_API void EndPanel();
ENGINE_API void Text(const char* fmt, ...);
```

- **`HudPanel`** (`Game/src/UI/HudPanel.cpp`) draws the top-left readout (selected
  block, the block under the crosshair, camera position/direction, FOV), the top-right
  performance panel (backend name, FPS, frame time, vertex/triangle counts, CPU/RAM,
  GPU/VRAM when available), the crosshair, and a fading save/load toast. It caches
  system metrics and refreshes them on a 0.5s timer, independent of whether the HUD is
  currently visible.
- **`SettingsPanel`** (`Game/src/UI/SettingsPanel.cpp`) renders general, culling, MSAA,
  render-distance, lighting, SSAO, IBL, and texture-filter sections, and pushes live
  edits straight to the light manager and block material. It owns the sun/ambient/fog
  slider state itself; crosshair, vsync, and multithreaded-loading toggles are read and
  written through `MainScene`, because `MainScene::SetupLighting` also reads them once
  at scene initialize time.
- **`Hotbar`** (`Game/src/UI/Hotbar.cpp`) lazily loads the nine slot icon textures on
  first render, then draws the strip; selection state lives on `BlockInteraction`, which
  `Hotbar` reads by reference rather than duplicating.

`MainScene` toggles all three at once with a single `m_showUI` flag bound to `F3`.

---

## 4. Controls

| Input | Action |
| :--- | :--- |
| `W` / `A` / `S` / `D` | Move forward / left / backward / right. |
| `Space` | Jump on ground, move up while flying. Double-tap toggles flight. |
| `Left Shift` | Sprint, on ground and while flying. |
| `Left/Right Ctrl` | Move down while flying. |
| `Left Mouse Button` | Break the targeted block. |
| `Right Mouse Button` | Place the selected hotbar block against the targeted face. |
| `Scroll Wheel` / `1`-`9` | Change the active hotbar slot. |
| `F3` | Toggle HUD, settings, and hotbar visibility. |
| `F5` | Save the world immediately. |
| `F6` | Reload the world from disk; if there are unsaved dirty chunks, the first press warns and a second press within 3 seconds confirms. |
| `Escape` | Save and return to the main menu. |

See @ref cli-reference for startup flags.

---

## 5. Where to Look in the Source

| Question | File |
| :--- | :--- |
| Movement, flight, and the collision call | `Game/src/Player/PlayerController.cpp` |
| Targeting, break, place, and hotbar selection | `Game/src/Player/BlockInteraction.cpp` |
| The animation that delays a placement | `Game/src/World/BlockEffects.cpp` |
| HUD readouts and the crosshair | `Game/src/UI/HudPanel.cpp` |
| Settings widgets and the sun handle | `Game/src/UI/SettingsPanel.cpp` |
| Hotbar layout and icons | `Game/src/UI/Hotbar.cpp` |
| Where input events are routed from | `Game/src/MainScene.cpp` |
