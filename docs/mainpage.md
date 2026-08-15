\htmlonly
<div class="se-hero">
<h1>SleakCraft</h1>
<p>A voxel sandbox built on SleakEngine. Procedural worlds, streamed chunks, and a fully data-driven voxel pipeline on Vulkan, OpenGL, and Direct3D.</p>
<div><a class="se-btn se-btn-primary" href="world-and-chunks.html">World and Chunks</a> <a class="se-btn se-btn-secondary" href="architecture-overview.html">Architecture</a></div>
</div>
<div class="se-cards">
<a class="se-card" href="architecture-overview.html"><b>Architecture Overview</b><span>Process lifecycle, the engine boundary, and the voxel vertex format registration.</span></a>
<a class="se-card" href="world-and-chunks.html"><b>World and Chunks</b><span>Chunk storage, streaming, meshing with AO, and single-pass MTV collision.</span></a>
<a class="se-card" href="world-generation.html"><b>World Generation</b><span>The 12-layer noise stack, biomes, caves, rivers, and lakes.</span></a>
<a class="se-card" href="persistence.html"><b>Persistence</b><span>Region files, world metadata, and the save and load orderings that keep worlds intact.</span></a>
<a class="se-card" href="player-and-ui.html"><b>Player and UI</b><span>The player controller, block interaction, and the HUD, settings, and hotbar panels.</span></a>
<a class="se-card" href="cli-reference.html"><b>Command-Line Reference</b><span>Every flag with worked examples for development and benchmarking.</span></a>
</div>
\endhtmlonly

![Procedurally generated terrain rendered by the Vulkan backend](sleakcraft_hero.jpg)

SleakCraft is a voxel sandbox game, in the shape of Minecraft, built on **SleakEngine**:
a C++23 engine with four interchangeable graphics backends (Vulkan, Direct3D 11,
Direct3D 12, OpenGL). The engine carries no voxel-specific code; SleakCraft is the one
project that defines what a voxel is, and it plugs that definition into the engine
through a generic custom-vertex-format API. That boundary, and how the rest of the game
is built on top of it, is the subject of @ref architecture-overview.


Engine documentation (rendering backends, the GameObject/Component model, physics, event system) lives in the
sibling **SleakEngine** repository and is built separately; see
`Engine/docs/html/index.html` once that build has run.

<h2>Building and running</h2>

```bash
cmake --preset debug && cmake --build --preset debug
cd bin
./SleakCraft -r vulkan -world Survival -seed 1337 -rd 12
```

The executable changes to its own directory on startup, so it must be run from `bin/`.
Asset and shader changes need a rebuild: the `POST_BUILD` step copies `Engine/assets/`
and `Game/assets/` into `bin/assets/`. There is no automated test suite in this
repository.

<h2>Code layout</h2>

```
SleakCraft/
├── Client/            entry point: Client/src/main.cpp
├── Game/               SleakGame, a shared library; game types are not namespaced
│   ├── include/
│   │   ├── Player/     PlayerController, BlockInteraction
│   │   ├── UI/         HudPanel, SettingsPanel, Hotbar
│   │   └── World/       chunk grid, generation, meshing, collision, persistence
│   ├── src/             implementation, mirroring include/
│   └── assets/shaders/  voxel + game-owned shaders (see @ref architecture-overview)
├── Engine -> ../SleakEngine   symlinked sibling checkout
└── docs/                this manual
```

`MainScene` (`Game/include/MainScene.hpp`) is the in-world gameplay scene. It is not a
god object: it constructs and wires together `ChunkManager`, `PlayerController`,
`BlockInteraction`, `BlockEffects`, `WorldPersistence`, and the three UI panels, and
routes input events to them, but the logic for each concern lives on that concern's own
class. There is no `Scenes/` directory; `MainScene` and `MainMenuScene` sit directly
under `Game/src` and `Game/include`.

<h2>Subsystem guides</h2>

- @ref architecture-overview "Architecture Overview", process lifecycle, the
  engine/game split, the vertex-format registration that lets a voxel-free engine
  render voxels, and the ownership rules that follow from it.
- @ref world-and-chunks "World and Chunks", chunk storage, the streaming loop that
  loads and unloads chunks around the player, face-culled meshing with ambient
  occlusion, column-mesh batching, and the voxel collision/raycast queries.
- @ref world-generation "World Generation", the noise layer stack, biome
  classification, and cave, river, and lake carving.
- @ref persistence "Persistence", the save directory layout, the region file
  format, and the save/load orchestration that ties them to the chunk grid.
- @ref player-and-ui "Player and UI", player movement and block interaction, and
  the HUD, settings, and hotbar panels built on the engine's ImGui wrapper.
- @ref cli-reference "Command-Line Reference", command-line flags and default
  in-game key bindings.
