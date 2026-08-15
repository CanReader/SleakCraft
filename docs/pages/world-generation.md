# World Generation {#world-generation}

`WorldGenerator` (`Game/src/World/WorldGenerator.cpp`) is a deterministic, seed-driven
terrain generator: the same seed always produces the same world, because every noise
layer is seeded from one base value with a fixed per-layer offset.

| Type | Role |
| :--- | :--- |
| `WorldGenerator` | Fills a chunk from noise and answers surface, cave, biome, and column-height queries. |
| `Noise` | Perlin and FBM sampling in 2D and 3D; one instance per layer, each with its own seed. |
| `Biome` | The biome enum surface and cave decoration select from. |
| `ColumnInfo` | The per-column values a generation pass computes once and reuses down the column. |
| `BlockType` | The block enum a generated cell resolves to. |

---

## 1. Noise Layers

`WorldGenerator::InitNoises` seeds twelve `Noise` instances (`Game/src/World/Noise.cpp`):

| Layer | Seed offset | Role |
| :--- | :--- | :--- |
| `m_continentalness` | `+0` | Large-scale land vs. ocean; also gates rivers and lakes. |
| `m_erosion` | `+1` | Mountain vs. flat terrain, combined with continentalness. |
| `m_peaksValleys` | `+2` | Mid-frequency height variation. |
| `m_detail` | `+3` | High-frequency surface detail. |
| `m_temperature` | `+4` | Biome selection. |
| `m_humidity` | `+5` | Biome selection. |
| `m_spaghettiA`, `m_spaghettiB` | `+6`, `+7` | Winding cave tunnels. |
| `m_cheese` | `+8` | Cavern-style cave carving. |
| `m_caveY` | `+9` | Vertical cave density falloff. |
| *(reserved)* | `+10` | Tree placement hash, a plain position hash, not a `Noise` instance. |
| `m_gravelNoise` | `+11` | Gravel patch placement. |
| `m_riverNoise` | `+12` | River channel carving. |
| `m_lakeNoise` | `+13` | Lake depression carving. |

> [!IMPORTANT]
> Seed offset `+10` is reserved for tree placement hashing. A new noise layer must use
> `+14` or higher.

`WorldGenerator::GetColumnInfo(worldX, worldZ)` evaluates continentalness and erosion
once per column and shares them between height and biome classification, then folds in
peaks/valleys and detail noise for height, and subtracts river/lake carve depth computed
from ridged samples of `m_riverNoise` and `m_lakeNoise`.

---

## 2. Biomes

```cpp
enum class Biome : uint8_t {
    Plains, Forest, Mountains, Desert, Beach, Ocean
};
```

Selection, in order, from `GetColumnInfo`:

1. `continent < -0.15` → Ocean.
2. `erosion > 0.2 && continent > 0.3` → Mountains.
3. `temp > 0.3 && humid < -0.1` → Desert.
4. `humid > 0.1` → Forest.
5. otherwise → Plains.

A later pass reclassifies non-Ocean, non-Desert columns within two blocks of sea level as
Beach. Tree density and species then vary by biome in the terrain fill pass: Forest gets
the highest density and can place spruce, other biomes place fewer trees or none.

---

## 3. Vertical Range

`MIN_CHUNK_Y = 0`, `MAX_CHUNK_Y = 7`: an 8-chunk-tall column, blocks `Y = 0` to `127`.
`SEA_LEVEL = BASE_HEIGHT = 64`. See @ref world-and-chunks for how
`ChunkManager::GetCachedColumnMaxCy` uses `GetMaxFilledChunkY` to skip queuing chunks
that would generate as empty air.

---

## 4. Caves, Rivers, Lakes

- **Caves** combine two ridged-noise "spaghetti" tunnel channels with a "cheese" cavern
  channel, gated by a vertical density falloff from `m_caveY` (`WorldGenerator::IsCave`).
- **Rivers** carve a channel where two ridged `m_riverNoise` samples both approach zero,
  gated to non-ocean continentalness.
- **Lakes** carve a depression where `m_lakeNoise` exceeds a threshold, also gated to
  non-ocean continentalness.

---

## 5. Adding a Layer

A new noise layer takes a seed offset of `+14` or higher. Offsets `+0`
through `+13` are taken by the table above, and `+10` is reserved even
though it is not a `Noise` instance: it seeds the position hash that places
trees. Reusing an occupied offset produces a layer perfectly correlated with
an existing one, which reads as terrain that mirrors itself rather than as
an obvious bug.

Determinism is the constraint every addition has to respect. Generation runs
on worker threads and the same chunk may be generated on any of them, so a
layer may only read the seed and the world position it is sampling at.
Anything that depends on generation order, on neighboring chunks, or on
wall-clock time breaks the guarantee that a seed reproduces a world, and
breaks the heightmap cache along with it, since the cache is stored against
the seed it was built for.

---

## 6. Where to Look in the Source

| Question | File |
| :--- | :--- |
| Layer seeding, terrain shaping, and decoration | `Game/src/World/WorldGenerator.cpp` |
| Perlin and FBM implementation | `Game/src/World/Noise.cpp` |
| Block and tile enumerations | `Game/include/World/Block.hpp` |
| How generated height feeds streaming | `Game/src/World/ChunkManager.cpp` |
| The cache that skips regenerating column heights | `Game/src/World/HeightmapCache.cpp` |
