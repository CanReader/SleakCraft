#ifndef _SAVE_MANAGER_HPP_
#define _SAVE_MANAGER_HPP_

#include "WorldMeta.hpp"
#include "RegionFile.hpp"
#include <string>
#include <unordered_map>
#include <array>

class ChunkManager;

struct ChunkCoord;
struct ChunkCoordHash;

class SaveManager {
public:
    void SetSavePath(const std::string& basePath);

    bool SaveWorld(const WorldMeta& meta,
                   const std::vector<ChunkSaveData>& dirtyChunks);
    bool LoadWorld(WorldMeta& meta,
                   std::unordered_map<int64_t, std::array<uint8_t, 4096>>& chunkData);

    bool HasSave() const;
    const std::string& GetSavePath() const { return m_savePath; }

private:
    bool EnsureDirectories() const;
    bool WriteWorldDat(const WorldMeta& meta,
                       const std::vector<ChunkSaveData>& dirtyChunks) const;
    bool ReadWorldDat(WorldMeta& meta) const;

    static int64_t PackCoord(int32_t cx, int32_t cy, int32_t cz);

    std::string m_savePath = "saves/Default";
};

#endif
