# World and Chunks {#world-and-chunks}

`ChunkManager` (`Game/include/World/ChunkManager.hpp`) owns the chunk grid and drives
streaming, edits, and save/load hooks; it delegates raycasts and collision to
`VoxelQueries`, meshing to `ChunkMesher`, and draw submission to `ChunkRenderer`. This
page follows a block from storage through streaming, meshing, and collision as one
narrative; generation is covered in @ref world-generation and disk format in
@ref persistence.

| Type | Role |
| :--- | :--- |
| `ChunkManager` | Owns the chunk grid, the worker pool, and the per-frame streaming budgets. |
| `Chunk` | One 16x16x16 block volume plus its six neighbor pointers and pending mesh data. |
| `WorldGenerator` | Fills a chunk from noise; also answers column height questions. |
| `ChunkMesher` | Merges a Y band of chunk meshes into one `ColumnMesh` and uploads it. |
| `ColumnMesh` | Merged solid and water handles, bounds, occluders, visibility and shadow flags. |
| `ChunkRenderer` | Per-frame visibility pass and draw submission for columns and water. |
| `VoxelQueries` | Raycast and collision against block data, bypassing engine colliders. |
| `HeightmapCache` | Persists the per-column max filled chunk-Y so generation can be skipped. |

\dot
digraph streaming {
  bgcolor="transparent"; rankdir=TB; compound=true;
  node [shape=box, style="rounded,filled", fillcolor="#1d4ed822", color="#3b82f6", fontcolor="#7aa7d9", fontname="Helvetica", fontsize=11];
  edge [color="#557799", fontcolor="#557799", fontname="Helvetica", fontsize=10];

  upd   [label="ChunkManager::Update(x, y, z)\ncalled from MainScene::Update", fillcolor="#22d3ee22", color="#22d3ee"];
  drain [label="1. drain m_readyQueue\nrelink neighbors,\nmark dirty columns"];
  unl   [label="2. gradual unload\nm_pendingUnload, stash dirty"];
  disp  [label="3. dispatch generation\nm_pendingLoad -> m_taskQueue"];
  rem   [label="4. dispatch remesh\nm_chunksNeedingRemesh"];
  upl   [label="5. GPU upload\nm_dirtyColumns, budget m_uploadsPerFrame"];

  subgraph cluster_worker {
    label="worker threads"; color="#22d3ee"; fontcolor="#7aa7d9";
    fontname="Helvetica"; fontsize=11; style=rounded;
    wt   [label="WorkerThread\nbatches of up to 8"];
    gen  [label="WorldGenerator::Generate(chunk)"];
    mesh [label="Chunk::GenerateMeshData()\nface culling, AO, occluder runs"];
    wt -> gen -> mesh;
  }

  rebuild [label="ChunkMesher::RebuildColumnMesh\nmerge band, MeshBatch::CreateMesh"];
  vis     [label="ChunkRenderer::UpdateVisibility\nsubmit occluders, IsVisible"];
  drawc   [label="ChunkRenderer::RenderColumns\nRenderWater", fillcolor="#22d3ee22", color="#22d3ee"];

  upd -> drain -> unl -> disp -> rem -> upl;
  disp -> wt [label="m_taskQueue + m_taskCV"];
  rem  -> wt;
  mesh -> drain [label="m_readyQueue"];
  upl -> rebuild -> vis -> drawc;
}
\enddot

Only generation and mesh-data construction cross onto the worker threads.
Every GPU touch stays on the main thread.

---

## 1. Chunk Storage

A `Chunk` (`Game/include/World/Chunk.hpp`) is one **16x16x16** block volume
(`Chunk::SIZE = 16`, `Chunk::VOLUME = 4096`), holding its blocks as a flat
`uint8_t[4096]` array and a pointer to each of its six face neighbors, used to see
across chunk boundaries during meshing. World height runs from chunk-Y `0` to
`WorldGenerator::MAX_CHUNK_Y = 7`: an 8-chunk column, i.e. blocks `Y = 0` to `127`.

`ChunkCoord { x, y, z }` addresses a chunk in chunk units. `ChunkManager` keeps the
loaded chunks in a flat `m_chunkGrid` sized to the current render distance, plus an
`m_activeChunks` list.

---

## 2. Streaming

`ChunkManager::Update(playerX, playerY, playerZ)` runs every frame and does three
things, each under its own per-frame budget:

- **Load/unload.** `m_loadSpiral` is a nearest-first XZ offset list built once by
  `BuildLoadSpiral()`. Each frame, `ChunkManager` walks it outward from the player's
  chunk and queues any missing chunk up to `GetCachedColumnMaxCy(cx, cz)` (the highest
  chunk-Y with generated content for that column, from the heightmap cache described
  below) into `m_pendingLoad`. Chunks that fall outside the render distance are queued
  into `m_pendingUnload` and drained gradually, with an over-cap safety valve that
  unloads faster than the backlog can grow.
- **Generation dispatch**, budgeted by `m_dispatchPerFrame` (default 64 chunks/frame).
- **Remesh and upload**, budgeted separately by `m_chunksPerFrame` (32, remesh + unload)
  and `m_uploadsPerFrame` (16, column mesh GPU uploads).

If `SetMultithreaded(true)` is active, generation and meshing run on a worker pool
(`StartWorkers` / `WorkerThread`): workers pull batches from `m_taskQueue`, generate and
mesh them, and publish to `m_readyQueue` for the main thread to drain; `StopWorkers`
joins every worker and requeues anything left in flight or completed-but-undrained so
nothing is stuck marked in-flight. A chunk must not be queued for meshing again while
`Chunk::IsInFlight()` is still true, or the same chunk can be dispatched twice.

`ChunkManager::FlushPendingChunks()` synchronously generates, links, and meshes every
pending chunk in one call; `MainScene` uses it once at spawn so the player has ground
under them immediately, instead of waiting for the streaming budgets to catch up.

Block edits go through `ChunkManager::SetBlockAt(x, y, z, type)`, which creates the
target chunk on demand and remeshes it plus any boundary neighbors and their column
meshes synchronously. If the target or a neighbor chunk is currently owned by a worker,
the edit is deferred into `m_pendingEdits` and retried on the next `Update`. What
reaches `SetBlockAt` is the confirmed edit, not the moment the player clicks: see
`BlockEffects` in @ref player-and-ui for the placement animation that sits in front of
it.

---

## 3. Meshing

`Chunk::GenerateMeshData()` is CPU-only and produces `VoxelVertex` geometry (see
@ref architecture-overview) plus occluder data, without touching the GPU or scene. It is
**per-block face culling with per-vertex ambient occlusion**, not greedy quad merging:
the chunk plus a one-block border is cached into an 18x18x18 opacity/solidity scratch
buffer, and for every solid block, each of the 6 neighbor cells is checked; a face is
emitted only if its neighbor is non-opaque. Each face's four corners get an independent
AO weight (`fastFaceAO`), sampled from the two edge-adjacent cells and the diagonal
corner cell and looked up in a four-step table:

```cpp
static constexpr float AO_TABLE[] = {0.40f, 0.68f, 0.88f, 1.0f};
```

The same pass extracts **occluder runs** for the culling system: per 8x8 quadrant of the
chunk's footprint, every vertical run of four or more fully-opaque layers is recorded
(not just the longest run), which preserves the solid shell that blocks sightlines from
a player standing on the surface into a cave below. Occluder boxes must only ever be
emitted over fully solid volumes, never over air or a cave, or the culling system will
hide geometry it should not.

`ChunkMesher` (`Game/include/World/ChunkMesher.hpp`) then merges a **band** of chunk
meshes along Y into one `ColumnMesh` per XZ column, cutting draw calls:

```cpp
inline constexpr int BAND_SIZE = 8;
inline int ChunkYToBand(int cy) {
    return (cy >= 0) ? cy / BAND_SIZE : (cy - BAND_SIZE + 1) / BAND_SIZE;
}
```

`BAND_SIZE` currently equals the full 8-chunk world height, so every column merges into
a single band today; the floor-division form still matters, because it has to map a
negative `cy` downward (`cy = -1` maps to band `-1`, not `0`) rather than truncating
toward zero, and must not be simplified. Each `ColumnMesh` holds the merged solid and
water `Sleak::MeshHandle`s, a visibility flag, a shadow-cast flag (turned off for distant
columns to skip the shadow pass), an exact-vertex-extent `Sleak::Math::AABB` bounds, and
the occluder AABBs collected from its chunks. `ColumnMesh::bounds` must stay an exact
extent: a bounds box that pokes above the real geometry makes every depth test against it
pass, which defeats occlusion culling while still paying its raster cost. Draw submission
is `ChunkRenderer::RenderColumns` / `RenderWater`.

---

## 4. The Heightmap Cache

`HeightmapCache` (`Game/include/World/HeightmapCache.hpp`) is a generation-skip cache,
not a lighting structure. `ChunkManager::GetCachedColumnMaxCy(cx, cz)` looks up, or
computes once via `WorldGenerator::GetMaxFilledChunkY` and caches, the highest chunk-Y
with any generated content for a column, replacing repeated noise evaluation (previously
up to 8 chunks times 9x6 FBM samples per column) with one cached lookup. The file
("HMCH" magic) is saved with the seed it was built for; loading discards it outright if
the magic or seed does not match, or if the stored entry count would read past EOF.

---

## 5. Collision and Raycast (`VoxelQueries`)

`VoxelQueries` (`Game/src/World/VoxelQueries.cpp`) queries block data directly through
`ChunkManager`, bypassing the engine's generic physics/collider components for terrain:

```cpp
VoxelRaycastResult VoxelRaycast(const Sleak::Math::Vector3D& origin,
                                 const Sleak::Math::Vector3D& direction,
                                 float maxDist) const;
VoxelCollisionResult ResolveVoxelCollision(const Sleak::Math::Vector3D& eyePos,
                                            float halfWidth, float height,
                                            float eyeOffset) const;
```

`VoxelRaycast` is a 3D-DDA march that steps cell by cell up to `maxDist`, stopping at the
first solid block and also reporting the empty cell just before it, which
`BlockInteraction` uses as the placement target.

`ResolveVoxelCollision` pushes a player-sized AABB out of every solid block it overlaps
with a **single-pass per-block minimum translation vector**: for each overlapping block
it computes penetration depth on X, Y, and Z at once and corrects along the shortest
axis, in one pass over the overlap set.

> [!IMPORTANT]
> This must stay single-pass. Splitting it into a Y-first phase produces a
> teleport-on-top bug, where the player snaps onto a block's top surface while walking
> into its side. Splitting it into an XZ-first phase produces ground-shake jitter on
> slopes. `PlayerController::Update` calls `ResolveVoxelCollision` every frame and
> applies its correction directly to the camera position; there is no intermediate
> physics step to absorb a bad resolution.

---

## 6. Threading Boundary

Exactly two calls run off the main thread: `WorldGenerator::Generate` and
`Chunk::GenerateMeshData`. A worker wakes on `m_taskCV`, takes up to eight
chunks off `m_taskQueue`, does both outside every lock, then appends to
`m_readyQueue` under `m_readyMutex`. Everything that touches the GPU stays
on the main thread, which is why `ChunkMesher` and `ChunkRenderer` have no
locking of their own.

Ownership handoff rides on `Chunk::IsInFlight()`. The main thread sets it
before enqueuing and clears it when draining; workers never write it. That
works precisely because only one thread mutates the flag, so a second
dispatch of an already-queued chunk has to be prevented by checking
`IsInFlight()` before queuing rather than by any atomic guard.

`Chunk::NeedsGeneration` is the one genuinely atomic flag, since
`GetBlockAt` reads it from the main thread to treat a mid-generation chunk
as air rather than reading half-written block data.

`ChunkMesher::RebuildColumnMesh` releases the old mesh handles before
allocating the new ones, and when both allocations fail it reports the
condition back through `ChunkManager::SetOOMThisFrame`, which puts the
column key back on the dirty set for a later frame instead of dropping it.

---

## 7. Where to Look in the Source

| Question | File |
| :--- | :--- |
| Streaming phases, budgets, and the worker pool | `Game/src/World/ChunkManager.cpp` |
| Face culling, AO, and occluder-run extraction | `Game/src/World/Chunk.cpp` |
| Band merging and GPU upload | `Game/src/World/ChunkMesher.cpp` |
| Visibility pass and draw submission | `Game/src/World/ChunkRenderer.cpp` |
| Raycast and collision resolution | `Game/src/World/VoxelQueries.cpp` |
| The column height cache format | `Game/src/World/HeightmapCache.cpp` |
| The voxel vertex struct and its registration | `Game/include/World/VoxelVertex.hpp` |
