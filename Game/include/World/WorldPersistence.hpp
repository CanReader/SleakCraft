#ifndef _WORLD_PERSISTENCE_HPP_
#define _WORLD_PERSISTENCE_HPP_

class ChunkManager;
class SaveManager;
class BlockEffects;
class MainScene;

/// Save/load orchestration for a MainScene's world: builds/consumes WorldMeta,
/// drains in-flight block-placement animations before collecting dirty
/// chunks, and drives the ForceReload -> SetSeed -> LoadChunkData ->
/// LoadHeightmapCache load ordering. Player-state and save-lock/UI-message
/// fields are read/written through the owning MainScene.
/// @ingroup persistence
class WorldPersistence {
public:
    WorldPersistence(ChunkManager& chunkManager, SaveManager& saveManager,
                      BlockEffects& blockEffects, MainScene& scene)
        : m_chunkManager(chunkManager),
          m_saveManager(saveManager),
          m_blockEffects(blockEffects),
          m_scene(scene) {}

    /// Drains completed placements, collects dirty chunks and player state
    /// into a WorldMeta, and writes it via SaveManager.
    void SaveGame();
    /// Reloads world.dat and region data, then reinitializes the chunk grid
    /// and player state from it.
    void LoadGame();

private:
    ChunkManager& m_chunkManager;
    SaveManager& m_saveManager;
    BlockEffects& m_blockEffects;
    MainScene& m_scene;
};

#endif
