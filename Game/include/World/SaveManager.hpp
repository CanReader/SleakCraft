#ifndef _SAVE_MANAGER_HPP_
#define _SAVE_MANAGER_HPP_

#include "WorldMeta.hpp"
#include "RegionFile.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <array>

class ChunkManager;

struct ChunkCoord;
struct ChunkCoordHash;

/// Reads/writes one world's on-disk save: world.dat (WorldMeta + region
/// index) plus the region files under regions/. Groups dirty chunks into
/// their RegionFile, merges with what's already on disk, and self-heals the
/// region index against the directory listing on every save/load.
/// @ingroup persistence
class SaveManager {
public:
    void SetSavePath(const std::string& basePath);

    /// Merges dirty chunks into their region files and rewrites world.dat.
    /// Fails closed: a region that can't be fully read back is left
    /// untouched and its chunks stay dirty for the next save attempt.
    bool SaveWorld(const WorldMeta& meta,
                   const std::vector<ChunkSaveData>& dirtyChunks);
    /// Reads world.dat, then loads every region file it (or a directory
    /// scan) references into a flat coordinate -> block-data map.
    bool LoadWorld(WorldMeta& meta,
                   std::unordered_map<int64_t, std::array<uint8_t, 4096>>& chunkData);

    bool HasSave() const;
    const std::string& GetSavePath() const { return m_savePath; }

    // Static utility methods for multi-world support
    /// Save directories under basePath that contain a world.dat.
    static std::vector<std::string> ListSaveDirectories(const std::string& basePath = "saves");
    /// Reads just world.dat's metadata, without touching region files.
    static bool ReadWorldMetaOnly(const std::string& savePath, WorldMeta& meta);
    static bool DeleteSaveDirectory(const std::string& path);

private:
    bool EnsureDirectories() const;
    /// Region coordinates recoverable from region filenames on disk, used to
    /// self-heal the world.dat region index.
    std::vector<std::pair<int, int>> ScanRegionDir() const;
    /// Merges the region index with existing/on-disk regions and atomically
    /// writes world.dat.
    bool WriteWorldDat(const WorldMeta& meta,
                       const std::vector<ChunkSaveData>& dirtyChunks) const;
    bool ReadWorldDat(WorldMeta& meta) const;

    /// Packs a chunk/region coordinate into one key; must stay identical to
    /// ChunkManager::PackCoord.
    static int64_t PackCoord(int32_t cx, int32_t cy, int32_t cz);

    std::string m_savePath = "saves/Default";
};

#endif
