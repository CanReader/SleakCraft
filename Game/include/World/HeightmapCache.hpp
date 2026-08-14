#ifndef _HEIGHTMAP_CACHE_HPP_
#define _HEIGHTMAP_CACHE_HPP_

#include <cstdint>
#include <string>
#include <unordered_map>

/// Per-column max filled chunk-Y, keyed by packed chunk XZ.
using ColumnMaxCyMap = std::unordered_map<uint64_t, int>;

/// Packs a chunk column XZ pair into the ColumnMaxCyMap key.
inline uint64_t PackColumnXZ(int cx, int cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32)
         |  static_cast<uint64_t>(static_cast<uint32_t>(cz));
}

/// HMCH-format persistence for the column max chunk-Y cache.
class HeightmapCache {
public:
    /// Writes the cache as magic/seed/count followed by packed
    /// (cx, cz, maxCy) triples; no-op if the cache is empty.
    static void Save(const std::string& path, const ColumnMaxCyMap& cache,
                     uint32_t seed);
    /// Loads entries into the cache, discarding the file if the magic or
    /// seed doesn't match or the entry count would run past EOF.
    static void Load(const std::string& path, ColumnMaxCyMap& cache,
                     uint32_t seed);
};

#endif
