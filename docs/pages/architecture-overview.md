# Architecture Overview {#architecture-overview}

How the process starts up, how SleakGame is layered on SleakEngine, and the rules that
keep the two decoupled.

| Type | Role |
| :--- | :--- |
| `Game` | The `Sleak::GameBase` subclass; owns both scenes and every transition between them. |
| `MainMenuScene` | World list, world creation, and deletion; starts a world one frame later. |
| `MainScene` | The playing scene; owns every world, player, and UI collaborator by value. |
| `ChunkManager` | The world: chunk grid, streaming, edits, and the query forwarders. |
| `WorldPersistence` | Save and load orchestration across the chunk grid and disk. |
| `PlayerController` / `BlockInteraction` | Movement and collision; block break, place, and hotbar selection. |
| `HudPanel` / `SettingsPanel` / `Hotbar` | The three UI panels, all built on `Sleak::UI` only. |
| `BlockEffects` | Break and place animations; the deferred source of confirmed placements. |
| `TextureAtlas` | All-static atlas builder and tile UV lookup; owned by nobody. |
| `VoxelVertex` | The 48-byte vertex struct SleakCraft registers with the engine. |

---

## 1. Layering

`SleakGame` is a CMake shared library; `Client` is the executable that links it and
constructs `Sleak::Application`. `Engine/` is a symlink to a sibling `SleakEngine`
checkout, and game code may only include from `Engine/include/public/`, never from
`Engine/src/` or `Engine/include/private/`. That boundary exists because SleakEngine is
shared: SleakSims, SleakEngine-Empty, and SleakEngine-FPP build against the same public
headers, so a game must not depend on anything the engine reserves the right to change.

```
Client/src/main.cpp
        |
        v
Game  (SleakGame)                 not namespaced: MainScene, ChunkManager, Chunk, ...
  MainScene (orchestrator)
    Player/  PlayerController, BlockInteraction
    UI/      HudPanel, SettingsPanel, Hotbar
    World/   ChunkManager, ChunkMesher, ChunkRenderer, VoxelQueries,
             WorldGenerator, Noise, HeightmapCache, WorldPersistence,
             SaveManager, RegionFile, WorldMeta, Block, TextureAtlas,
             BlockEffects, VoxelVertex
        |
        v
SleakEngine public API (Sleak::)   namespaced
  Scene / GameObject / ECS components, VertexFormatRegistry, MeshBatch,
  PhysicsWorld, InputManager, Application
        |
        v
Graphics backend selected by -r: vulkan | d3d11 | d3d12 | opengl
```

Two further rules follow from the boundary:

- **No ImGui in Game.** ImGui is vendored inside the engine and linked PRIVATE. Game
  code goes through `<UI/UI.hpp>` (`Sleak::UI::BeginPanel`, `Text`, `EndPanel`, and a
  handful of widgets); see @ref player-and-ui for how the HUD and settings panels use it.
- **No renamed public symbols.** Changing a `Sleak::` public API changes it for every
  project that consumes the engine, not just this one.

---

## 2. Startup and Scene Lifecycle

`Client/src/main.cpp` sets the working directory to the executable's own folder (so
relative asset paths resolve regardless of how it was launched), registers the CLI help
callback, parses arguments with `Sleak::CommandLine::Parse`, and hands a `Game` instance
to `Sleak::Application::Run`.

`Game` (`Game/src/Game.cpp`) drives three transitions:

- `Game::Initialize()` registers the voxel vertex format
  (`GetVoxelVertexFormat()`, see below) before any chunk mesh can be built, then creates
  and activates `MainMenuScene`.
- `Game::Begin()` is compiled only in Debug builds. If `-world <name>` was passed, it
  reads `-seed` for a new world (or opens the existing save) and calls
  `Game::StartWorld` directly, skipping the main menu.
- `Game::StartWorld` and `Game::ReturnToMenu` both call
  `Sleak::Application::WaitGPUIdle()` before destroying the outgoing scene. Swapping a
  scene while the GPU still has commands in flight against its buffers is a driver
  crash, not a leak; any future scene-switch path must flush first, the same way these
  two do.

---

## 3. Scene and Component Model

`Game` owns two scenes and nothing else. `MainScene` is where all the real
collaborators live, held as by-value members in declaration order, so
construction order is the order they appear in the header:

\dot
digraph collab {
  bgcolor="transparent"; rankdir=TB;
  node [shape=box, style="rounded,filled", fillcolor="#1d4ed822", color="#3b82f6", fontcolor="#7aa7d9", fontname="Helvetica", fontsize=11];
  edge [color="#557799", fontcolor="#557799", fontname="Helvetica", fontsize=10];

  game [label="Game : Sleak::GameBase", fillcolor="#22d3ee22", color="#22d3ee"];
  menu [label="MainMenuScene\nworld list, create, delete"];
  main [label="MainScene : Sleak::Scene", fillcolor="#22d3ee22", color="#22d3ee"];

  subgraph cluster_world {
    label="World"; color="#3b82f6"; fontcolor="#7aa7d9";
    fontname="Helvetica"; fontsize=11; style=rounded;
    cm  [label="ChunkManager\nowns WorldGenerator, ChunkMesher,\nChunkRenderer, VoxelQueries"];
    be  [label="BlockEffects"];
    sm  [label="SaveManager"];
    wp  [label="WorldPersistence"];
  }

  subgraph cluster_player {
    label="Player"; color="#3b82f6"; fontcolor="#7aa7d9";
    fontname="Helvetica"; fontsize=11; style=rounded;
    pc  [label="PlayerController"];
    bi  [label="BlockInteraction"];
  }

  subgraph cluster_ui {
    label="UI"; color="#3b82f6"; fontcolor="#7aa7d9";
    fontname="Helvetica"; fontsize=11; style=rounded;
    hud [label="HudPanel"];
    set [label="SettingsPanel"];
    hb  [label="Hotbar"];
  }

  game -> menu;
  game -> main [label="StartWorld / ReturnToMenu"];
  menu -> game [label="StartWorld(...)", style=dashed];
  main -> cm; main -> be; main -> sm; main -> wp;
  main -> pc; main -> bi;
  main -> hud; main -> set; main -> hb;
  wp -> cm [label="uses", style=dashed];
  wp -> sm [style=dashed];
  wp -> be [style=dashed];
  pc -> cm [style=dashed];
  bi -> cm [style=dashed];
  bi -> be [style=dashed];
  hud -> cm [style=dashed];
  set -> cm [style=dashed];
  hb -> bi [style=dashed];
}
\enddot

Solid arrows are ownership, dashed arrows are references injected at
construction. Every collaborator that needs the world holds a
`ChunkManager&`, and none of them owns it.

SleakEngine's object model is a `Sleak::Scene` owning `Sleak::GameObject` instances,
each carrying `Sleak::Component`-derived pieces (`Sleak::TransformComponent`,
`Sleak::MeshComponent`, `Sleak::CameraController`, `Sleak::RigidbodyComponent`,
`Sleak::ColliderComponent`, ...). `MainScene` is itself a `Sleak::Scene` subclass, and
individual `Chunk`s each own a `Sleak::GameObject` for scene attachment
(`Chunk::AddToScene` / `RemoveFromScene`). Chunk geometry does not flow through
`MeshComponent`, though: `ChunkMesher` builds `Sleak::MeshHandle`s directly and submits
them through `Sleak::MeshBatch`, because per-object component overhead does not scale to
thousands of chunk draws. See @ref world-and-chunks.

Objects are owned by the `Scene` that holds them; destroy them with
`Scene::DestroyObject()`, never `delete`. Engine-allocated resources (textures, meshes,
vertex layouts) are held behind `Sleak::RefPtr<T>`, not `std::shared_ptr`.

---

## 4. The Vertex Format Boundary

SleakEngine exposes a layout-agnostic vertex registration API
(`Sleak::VertexFormatRegistry`, `Runtime/VertexLayout.hpp`) instead of a fixed vertex
struct. A `VertexLayoutDesc` describes byte stride, a list of attributes (shader
location, format, byte offset), and up to four shader stems, one per render pass:

```cpp
struct VertexLayoutDesc {
    uint32_t                     stride = 0;
    std::vector<VertexAttribute> attributes;
    std::string                  shaderStem;             // forward/main variant
    std::string                  shadowShaderStem;        // empty skips the shadow pass
    std::string                  gbufferShaderStem;       // empty is forward-only
    std::string                  transparentShaderStem;   // empty skips the transparent pass
};
```

An empty stem is not a fallback to a default shader; it skips that pass for the format
entirely, and a stem that fails to compile is logged as an error, not silently
substituted. `VertexFormatRegistry::Register` returns a `VertexFormatHandle` (a plain
`uint32_t`; `0` is the engine's built-in default vertex, registered formats start at
`1`). Shader stems are consumed by the Vulkan backend only: OpenGL binds a registered
layout's attributes to its own fixed shader programs instead, which is why
`Game/assets/shaders` has no `_gl` variant of `gbuffer_voxel` or `shadow_depth_voxel`.

SleakCraft is the sole consumer of this API. It owns `VoxelVertex`
(`Game/include/World/VoxelVertex.hpp`), a 48-byte struct, and registers it once through a
function-local static:

```cpp
inline Sleak::VertexFormatHandle GetVoxelVertexFormat() {
    static const Sleak::VertexFormatHandle handle = [] {
        Sleak::VertexLayoutDesc desc;
        desc.stride = sizeof(VoxelVertex);
        desc.attributes = {
            {0, Sleak::VertexAttribFormat::Float3, offsetof(VoxelVertex, px)}, // position
            {1, Sleak::VertexAttribFormat::Float3, offsetof(VoxelVertex, nx)}, // normal
            {2, Sleak::VertexAttribFormat::Float4, offsetof(VoxelVertex, r)},  // color/AO
            {3, Sleak::VertexAttribFormat::Float2, offsetof(VoxelVertex, u)},  // texcoord
        };
        desc.shaderStem            = "flat_shader";
        desc.shadowShaderStem      = "shadow_depth_voxel";
        desc.gbufferShaderStem     = "gbuffer_voxel";
        desc.transparentShaderStem = "water_shader";
        return Sleak::VertexFormatRegistry::Register(desc);
    }();
    return handle;
}
```

`Sleak::MeshBatch::CreateMesh(VertexFormatHandle, const void* vertexData,
size_t vertexBytes, const uint32_t* indices, size_t indexCount)` builds the GPU buffers
for one mesh keyed to that handle; the resulting `Sleak::MeshHandle` carries the handle
so the renderer binds the right pipeline automatically. `ChunkMesher` is the only caller
for voxel geometry (@ref world-and-chunks).

The four backend variants of a shader (`name.vert`/`name.frag` plus compiled `.spv` for
Vulkan, `name_gl.vert`/`name_gl.frag` for OpenGL, `name.hlsl` for DirectX 11,
`name_dx12.hlsl` for DirectX 12) must be edited together. Voxel shaders live in
`Game/assets/shaders`, not the engine; running `./scripts/compile_shaders.sh` after
editing any of them is required; it compiles SPIR-V for both `Engine/assets/shaders`
and `Game/assets/shaders`. The engine's own `CompileShaders` CMake target only watches
`Engine/assets/shaders` and never sees the game's shader folder.

---

## 5. The Per-Frame Order in `MainScene`

`MainScene::Update` runs a fixed sequence, and several steps depend on the
one before them:

```
BlockEffects::Update                    // advance break/place animations
BlockInteraction::DrainCompletedPlacements   // finished placements become real blocks
Sleak::Scene::Update                     // engine objects, lights, physics
HudPanel::Update
PlayerController::Update                 // movement + voxel collision
ChunkManager::Update                     // streaming, meshing, visibility
ChunkManager::RenderColumns
water material tiling, then ChunkManager::RenderWater
BlockInteraction::RenderOutline
auto-save check, message timers
HudPanel::RenderCrosshair, HudPanel::Render, SettingsPanel::Render, Hotbar::Render
```

Placements drain before the engine update so a block finishing its animation
this frame is solid by the time collision runs. The player moves before
`ChunkManager::Update` so streaming centers on the position the player
actually reached. Water submits after the solid columns and after its
material tiling is advanced by the game clock, which is what animates the
surface.

---

## 6. Where to Look in the Source

| Question | File |
| :--- | :--- |
| Process entry, working directory, CLI parsing | `Client/src/main.cpp` |
| Scene transitions and the GPU flush before each | `Game/src/Game.cpp` |
| The per-frame order and input routing | `Game/src/MainScene.cpp` |
| World list, creation, and deferred start | `Game/src/MainMenuScene.cpp` |
| The voxel vertex struct and its registration | `Game/include/World/VoxelVertex.hpp` |
| Atlas build order against the tile enum | `Game/include/World/Block.hpp`, `Game/src/World/TextureAtlas.cpp` |
