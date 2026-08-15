#include "World/HeightmapCache.hpp"
#include <fstream>

// Format: magic(4) seed(4) count(4) [cx(4) cz(4) maxCy(4)] * count
// Tied to seed — if seed mismatches the file is silently ignored.

static constexpr uint32_t HEIGHTMAP_MAGIC = 0x484D4348; // "HMCH"

void HeightmapCache::Save(const std::string& path, const ColumnMaxCyMap& cache,
                          uint32_t seed) {
    if (cache.empty()) return;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return;

    uint32_t count = static_cast<uint32_t>(cache.size());

    f.write(reinterpret_cast<const char*>(&HEIGHTMAP_MAGIC), 4);
    f.write(reinterpret_cast<const char*>(&seed),  4);
    f.write(reinterpret_cast<const char*>(&count), 4);

    for (const auto& [packed, maxCy] : cache) {
        // Unpack cx/cz from the uint64 key (same packing as PackColumnXZ)
        int32_t cx = static_cast<int32_t>(static_cast<uint32_t>(packed >> 32));
        int32_t cz = static_cast<int32_t>(static_cast<uint32_t>(packed & 0xFFFFFFFF));
        int32_t mc = static_cast<int32_t>(maxCy);
        f.write(reinterpret_cast<const char*>(&cx), 4);
        f.write(reinterpret_cast<const char*>(&cz), 4);
        f.write(reinterpret_cast<const char*>(&mc), 4);
    }
}

void HeightmapCache::Load(const std::string& path, ColumnMaxCyMap& cache,
                          uint32_t expectedSeed) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return;

    auto fileSize = static_cast<size_t>(f.tellg());
    if (fileSize < 12) return;
    f.seekg(0);

    uint32_t magic, seed, count;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&seed),  4);
    f.read(reinterpret_cast<char*>(&count), 4);

    if (magic != HEIGHTMAP_MAGIC) return;
    if (seed  != expectedSeed) return;  // stale cache — different seed
    if (fileSize < 12 + static_cast<size_t>(count) * 12) return;  // truncated

    cache.reserve(cache.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t cx, cz, maxCy;
        f.read(reinterpret_cast<char*>(&cx),    4);
        f.read(reinterpret_cast<char*>(&cz),    4);
        f.read(reinterpret_cast<char*>(&maxCy), 4);
        uint64_t key = PackColumnXZ(cx, cz);
        cache.emplace(key, static_cast<int>(maxCy));
    }
}
