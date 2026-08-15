# Persistence {#persistence}

The on-disk save format and the code that keeps it in sync with the chunk grid.

| Type | Role |
| :--- | :--- |
| `WorldPersistence` | Orchestrates a save or load across the chunk grid, block effects, and disk. |
| `SaveManager` | Owns I/O for one save directory: `world.dat` plus its region files. |
| `RegionFile` | All-static reader and writer for one 8x8 region file, with RLE and CRC32. |
| `ChunkSaveData` | One chunk on disk: coordinates plus its 4096 block bytes. |
| `WorldMeta` | Header record: version, seed, world name, player state, region index. |
| `WorldMeta::PlayerState` | Position, pitch, yaw, selected block, render distance. |
| `HeightmapCache` | Separate seed-tagged cache file, saved alongside the world. |

\dot
digraph savepath {
  bgcolor="transparent"; rankdir=TB;
  node [shape=box, style="rounded,filled", fillcolor="#1d4ed822", color="#3b82f6", fontcolor="#7aa7d9", fontname="Helvetica", fontsize=11];
  edge [color="#557799", fontcolor="#557799", fontname="Helvetica", fontsize=10];

  trig [label="F5, window close, auto-save timer,\nreturn to menu"];
  ms   [label="MainScene::SaveGame()"];
  wp   [label="WorldPersistence::SaveGame()", fillcolor="#22d3ee22", color="#22d3ee"];
  drain[label="BlockEffects::DrainAllPlacements\nChunkManager::SetBlockAt\nChunkManager::FlushPendingEdits"];
  meta [label="build WorldMeta\nseed, player state, render distance"];
  dirty[label="ChunkManager::GetDirtyChunks\n-> vector<ChunkSaveData>"];
  sm   [label="SaveManager::SaveWorld(meta, dirtyChunks)"];
  grp  [label="group by region\nRegionFile::RegionCoord"];
  rl   [label="RegionFile::Load\nmerge with existing chunks"];
  rs   [label="RegionFile::Save\nRLE + CRC32, temp file,\nfsync, rename", fillcolor="#22d3ee22", color="#22d3ee"];
  wd   [label="WriteWorldDat\nregion index, atomic write"];
  hm   [label="ChunkManager::SaveHeightmapCache\nClearDirtyFlags"];

  trig -> ms -> wp -> drain -> meta -> dirty -> sm;
  sm -> grp -> rl -> rs;
  sm -> wd;
  rs -> hm [label="on success"];
  wd -> hm;
}
\enddot

Every write lands through a temp file, an fsync, and a rename, and a region
that cannot be read back is skipped with its chunks left dirty rather than
overwritten.

---

## 1. Save Directory Layout

A save lives under `saves/<worldName>/`:

```
saves/<worldName>/
├── world.dat            one WorldMeta record
├── regions/
│   └── r.<rx>.<rz>.dat  one RegionFile per 8x8 chunk-column region
└── heightmap.cache      HeightmapCache (see @ref world-and-chunks)
```

---

## 2. Region Files (`RegionFile`)

`Game/include/World/RegionFile.hpp`, `Game/src/World/RegionFile.cpp`:

```cpp
static constexpr uint32_t MAGIC = 0x534C4B52;  // "SLKR"
static constexpr uint16_t CURRENT_VERSION = 1;
static constexpr int REGION_SIZE = 8;
```

`RegionFile::RegionCoord` floor-divides a chunk coordinate by `REGION_SIZE` to get its
region coordinate; `RegionFileName(rx, rz)` formats the file as `r.<rx>.<rz>.dat`. Each
chunk is stored as a `ChunkSaveData { cx, cy, cz, blocks[4096] }`, run-length encoded
(`RegionFile::RLEEncode`, `count:u16, value:u8` pairs) and checksummed with a per-chunk
CRC32. `RegionFile::Load` treats a chunk that fails RLE decoding or its CRC check as
recoverable: it drops that one chunk (counted via an optional `droppedChunks` out
parameter) instead of failing the whole file, and only returns `false` for structural
corruption of the file itself. `RegionFile::Save` writes atomically, through a
temp-file-plus-fsync-plus-rename, so a crash mid-save cannot corrupt an existing region
file.

---

## 3. `SaveManager`

`Game/src/World/SaveManager.cpp` owns I/O for one save directory:

```cpp
bool SaveWorld(const WorldMeta& meta, const std::vector<ChunkSaveData>& dirtyChunks);
bool LoadWorld(WorldMeta& meta,
               std::unordered_map<int64_t, std::array<uint8_t, 4096>>& chunkData);
```

`SaveWorld` groups the dirty chunks by region, merges each region's existing on-disk
chunks with the dirty ones, and rewrites the region file; a region that cannot be fully
read back is left untouched and its chunks stay dirty for the next attempt, so a bad
region never destroys data that was previously saved successfully. `LoadWorld` reads
`world.dat`, then loads every region it references (falling back to a directory scan to
self-heal a stale region index) into a flat coordinate-to-block-data map.

Both `SaveManager::PackCoord` and `ChunkManager::PackCoord` pack a chunk coordinate into
one `int64_t` map key and must implement the identical formula, or lookups between the
two silently miss:

```cpp
uint64_t ux = static_cast<uint32_t>(cx);
uint64_t uy = static_cast<uint32_t>(cy) & 0xFFFF;
uint64_t uz = static_cast<uint32_t>(cz);
int64_t key = static_cast<int64_t>((ux << 32) | (uy << 16) | (uz & 0xFFFF));
```

Static helpers round out multi-world support: `ListSaveDirectories` enumerates save
folders that contain a `world.dat`, `ReadWorldMetaOnly` reads just the metadata without
touching region files, `DeleteSaveDirectory` removes a save.

---

## 4. `WorldMeta`

`Game/include/World/WorldMeta.hpp`: magic `0x534C4B57` ("SLKW"), `CURRENT_VERSION = 1`.

```cpp
struct WorldMeta {
    uint16_t version = CURRENT_VERSION;
    uint16_t flags = 0;
    int64_t saveTimestamp = 0;
    std::string worldName = "Default";
    uint32_t seed = 0;
    PlayerState player;             // posX/Y/Z, pitch, yaw, selectedBlock, renderDistance
    std::vector<RegionEntry> regions;  // {rx, rz, chunkCount}
};
```

Any field change must bump `CURRENT_VERSION`. `SaveManager::ReadWorldDat` currently
rejects a mismatched version outright, so introducing a new version needs an explicit
legacy-load path added there at the same time, not after.

---

## 5. Save/Load Orchestration (`WorldPersistence`)

`WorldPersistence` (`Game/src/World/WorldPersistence.cpp`) sits between `MainScene` and
`ChunkManager` / `SaveManager` / `BlockEffects`:

- `SaveGame()` drains completed block-placement animations (`BlockEffects`), collects
  dirty chunks and current player state into a `WorldMeta`, writes it through
  `SaveManager::SaveWorld`, and calls `ChunkManager::SaveHeightmapCache`.
- `LoadGame()` reloads `world.dat` and region data, then reinitializes the chunk grid and
  player state, driving the fixed order `ForceReload -> SetSeed -> LoadChunkData ->
  LoadHeightmapCache`. That order matters: `ForceReload` drops the existing chunk grid
  before anything is reseeded, and the heightmap cache is only meaningful once the seed
  and chunk data it was built against are in place.

`MainScene` binds this to the `F5` (save) and `F6` (reload) keys; see
@ref cli-reference.

Four things trigger a save, and all of them funnel through the same
`MainScene::SaveGame()` entry point: the `F5` key, the window close event,
the 120-second auto-save timer in `MainScene::Update`, and
`Game::ReturnToMenu`. There is no separate quit-save path that could diverge
from the manual one.

The full load order is longer than the four-call core, because the render
distance is deliberately collapsed and restored around it:

```
ForceReload -> SetSeed(meta.seed) -> LoadChunkData -> LoadHeightmapCache
  -> SetRenderDistance(3) -> Update(...) -> FlushPendingChunks()
  -> SetRenderDistance(rd)  // clamped 4..16, -rd overrides
  -> SetMultithreaded(...)
```

Dropping to a render distance of 3 before the synchronous flush means the
player gets ground underneath them from a handful of chunks rather than the
full ring, and the real distance is restored afterwards so streaming fills
in the rest in the background.

---

## 6. Failure Handling

`WorldPersistence` treats a failed load as a reason to stop writing. If
`LoadWorld` fails while a save directory exists on disk, the scene is put
into a save-locked state and every later save attempt returns early with a
message rather than overwriting what is there. A corrupt save stays
recoverable by hand instead of being replaced by a fresh one.

At the region level, failure is finer-grained. A chunk whose RLE stream or
CRC32 does not check out is dropped on its own and counted, and the rest of
the region loads normally. When any chunk is dropped, `SaveManager` copies
the region aside with a `.corrupt` suffix before rewriting it. A region that
fails structurally is a different case: it is left completely untouched and
its dirty chunks stay dirty, so the next save tries again rather than
committing a partial merge.

---

## 7. Where to Look in the Source

| Question | File |
| :--- | :--- |
| Save and load orchestration order | `Game/src/World/WorldPersistence.cpp` |
| Region grouping, merging, and `world.dat` | `Game/src/World/SaveManager.cpp` |
| The region binary format, RLE, and CRC32 | `Game/src/World/RegionFile.cpp` |
| Field layout and the version constant | `Game/include/World/WorldMeta.hpp` |
| Where dirty chunks come from | `Game/src/World/ChunkManager.cpp` |
| The keys and timer that trigger a save | `Game/src/MainScene.cpp` |
