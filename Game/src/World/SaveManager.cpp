#include "World/SaveManager.hpp"
#include <fstream>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <sys/stat.h>
#include <Logger.hpp>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

// ── Atomic write: temp file + fsync + rename; target intact on failure ─

static bool AtomicWriteFile(const std::string& path,
                            const std::vector<uint8_t>& buf) {
    std::string tmp = path + ".tmp";
    std::FILE* fp = std::fopen(tmp.c_str(), "wb");
    if (!fp) {
        SLEAK_ERROR("Save: cannot open temp file {}", tmp);
        return false;
    }
    bool ok = buf.empty() ||
              std::fwrite(buf.data(), 1, buf.size(), fp) == buf.size();
    if (ok) ok = (std::fflush(fp) == 0);
    if (ok) {
#ifdef _WIN32
        ok = (_commit(_fileno(fp)) == 0);
#else
        ok = (::fsync(::fileno(fp)) == 0);
#endif
    }
    std::fclose(fp);
    std::error_code ec;
    if (!ok) {
        SLEAK_ERROR("Save: write/fsync failed for {}", tmp);
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        SLEAK_ERROR("Save: rename {} -> {} failed: {}", tmp, path, ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

// ── Binary write helpers ─────────────────────────────────────────────

static void WriteU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }

static void WriteU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static void WriteU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void WriteI32(std::vector<uint8_t>& buf, int32_t v) {
    WriteU32(buf, static_cast<uint32_t>(v));
}

static void WriteI64(std::vector<uint8_t>& buf, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
}

static void WriteFloat(std::vector<uint8_t>& buf, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    WriteU32(buf, bits);
}

static void WriteString(std::vector<uint8_t>& buf, const std::string& s) {
    WriteU32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ── Binary read helpers ──────────────────────────────────────────────

static bool ReadU8(const uint8_t*& p, const uint8_t* end, uint8_t& v) {
    if (p + 1 > end) return false;
    v = *p++;
    return true;
}

static bool ReadU16(const uint8_t*& p, const uint8_t* end, uint16_t& v) {
    if (p + 2 > end) return false;
    v = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    p += 2;
    return true;
}

static bool ReadU32(const uint8_t*& p, const uint8_t* end, uint32_t& v) {
    if (p + 4 > end) return false;
    v = static_cast<uint32_t>(p[0])
      | (static_cast<uint32_t>(p[1]) << 8)
      | (static_cast<uint32_t>(p[2]) << 16)
      | (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

static bool ReadI32(const uint8_t*& p, const uint8_t* end, int32_t& v) {
    uint32_t u;
    if (!ReadU32(p, end, u)) return false;
    v = static_cast<int32_t>(u);
    return true;
}

static bool ReadI64(const uint8_t*& p, const uint8_t* end, int64_t& v) {
    if (p + 8 > end) return false;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i)
        u |= static_cast<uint64_t>(p[i]) << (i * 8);
    p += 8;
    v = static_cast<int64_t>(u);
    return true;
}

static bool ReadFloat(const uint8_t*& p, const uint8_t* end, float& v) {
    uint32_t bits;
    if (!ReadU32(p, end, bits)) return false;
    std::memcpy(&v, &bits, sizeof(v));
    return true;
}

static bool ReadString(const uint8_t*& p, const uint8_t* end, std::string& s) {
    uint32_t len;
    if (!ReadU32(p, end, len)) return false;
    if (p + len > end) return false;
    s.assign(reinterpret_cast<const char*>(p), len);
    p += len;
    return true;
}

// ── Directory creation ───────────────────────────────────────────────

static bool MakeDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return true;
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

static bool MakeDirRecursive(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/' || path[i] == '\\' || i == path.size() - 1) {
            if (!current.empty() && current != "/" && current != "\\")
                MakeDir(current);
        }
    }
    return true;
}

// ── SaveManager ──────────────────────────────────────────────────────

void SaveManager::SetSavePath(const std::string& basePath) {
    m_savePath = basePath;
}

bool SaveManager::HasSave() const {
    std::error_code ec;
    auto size = std::filesystem::file_size(m_savePath + "/world.dat", ec);
    return !ec && size > 0;
}

// ── Region directory scan: index is reconstructible from filenames ───

std::vector<std::pair<int, int>> SaveManager::ScanRegionDir() const {
    std::vector<std::pair<int, int>> found;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(m_savePath + "/regions", ec)) {
        int rx, rz;
        std::string name = entry.path().filename().string();
        if (std::sscanf(name.c_str(), "r.%d.%d.dat", &rx, &rz) == 2 &&
            name == RegionFile::RegionFileName(rx, rz))
            found.emplace_back(rx, rz);
    }
    return found;
}

bool SaveManager::EnsureDirectories() const {
    MakeDirRecursive(m_savePath + "/regions");
    return true;
}

int64_t SaveManager::PackCoord(int32_t cx, int32_t cy, int32_t cz) {
    // Pack 3 ints into a single int64 for map key
    // cy is limited range, cx/cz are world coords
    uint64_t ux = static_cast<uint32_t>(cx);
    uint64_t uy = static_cast<uint32_t>(cy) & 0xFFFF;
    uint64_t uz = static_cast<uint32_t>(cz);
    return static_cast<int64_t>((ux << 32) | (uy << 16) | (uz & 0xFFFF));
}

// ── Save ─────────────────────────────────────────────────────────────

bool SaveManager::SaveWorld(const WorldMeta& meta,
                            const std::vector<ChunkSaveData>& dirtyChunks) {
    EnsureDirectories();

    // Group dirty chunks by region
    std::unordered_map<int64_t, std::vector<const ChunkSaveData*>> regionGroups;
    std::unordered_map<int64_t, std::pair<int,int>> regionCoords;

    for (const auto& chunk : dirtyChunks) {
        int rx, rz;
        RegionFile::RegionCoord(chunk.cx, chunk.cz, rx, rz);
        int64_t key = PackCoord(rx, 0, rz);
        regionGroups[key].push_back(&chunk);
        regionCoords[key] = {rx, rz};
    }

    // For each region: load existing data, merge dirty chunks, save
    for (auto& [key, dirtyList] : regionGroups) {
        auto [rx, rz] = regionCoords[key];
        std::string regionPath = m_savePath + "/regions/" + RegionFile::RegionFileName(rx, rz);

        // Fail closed: never rewrite a region we could not fully read
        std::vector<ChunkSaveData> existing;
        size_t dropped = 0;
        std::error_code ec;
        if (std::filesystem::exists(regionPath, ec) &&
            !RegionFile::Load(regionPath, existing, &dropped)) {
            SLEAK_ERROR("Save: region {} unreadable — keeping it intact, "
                        "chunks stay dirty for retry", regionPath);
            return false;
        }
        if (dropped > 0) {
            std::string bak = regionPath + ".corrupt";
            if (!std::filesystem::exists(bak, ec))
                std::filesystem::copy_file(regionPath, bak, ec);
            SLEAK_WARN("Save: {} chunk(s) dropped from {} — original kept at {}",
                       dropped, regionPath, bak);
        }

        // Build map of existing chunks, overwrite with dirty ones
        std::unordered_map<int64_t, ChunkSaveData> merged;
        for (auto& c : existing)
            merged[PackCoord(c.cx, c.cy, c.cz)] = c;
        for (auto* c : dirtyList)
            merged[PackCoord(c->cx, c->cy, c->cz)] = *c;

        // Convert back to vector
        std::vector<ChunkSaveData> toSave;
        toSave.reserve(merged.size());
        for (auto& [k, v] : merged)
            toSave.push_back(std::move(v));

        if (!RegionFile::Save(regionPath, toSave))
            return false;
    }

    // Build region index from dirty chunks for world.dat
    WorldMeta metaCopy = meta;
    metaCopy.regions.clear();
    for (auto& [key, coords] : regionCoords) {
        auto& [rx, rz] = coords;
        metaCopy.regions.push_back({rx, rz, static_cast<int32_t>(regionGroups[key].size())});
    }

    return WriteWorldDat(metaCopy, dirtyChunks);
}

bool SaveManager::WriteWorldDat(const WorldMeta& meta,
                                const std::vector<ChunkSaveData>& /*dirtyChunks*/) const {
    // First, read existing world.dat to get previous region list
    WorldMeta existing;
    std::unordered_map<int64_t, WorldMeta::RegionEntry> allRegions;

    if (const_cast<SaveManager*>(this)->ReadWorldDat(existing)) {
        for (const auto& r : existing.regions)
            allRegions[PackCoord(r.rx, 0, r.rz)] = r;
    }

    // Self-heal: union region files present on disk but missing from index
    for (auto& [rx, rz] : ScanRegionDir()) {
        int64_t key = PackCoord(rx, 0, rz);
        if (!allRegions.count(key)) allRegions[key] = {rx, rz, 0};
    }

    // Merge new regions (overwrite counts for updated regions)
    for (const auto& r : meta.regions)
        allRegions[PackCoord(r.rx, 0, r.rz)] = r;

    std::vector<uint8_t> buf;

    WriteU32(buf, WorldMeta::MAGIC);
    WriteU16(buf, meta.version);
    WriteU16(buf, meta.flags);

    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    WriteI64(buf, timestamp);

    WriteString(buf, meta.worldName);
    WriteU32(buf, meta.seed);

    // Player state
    WriteFloat(buf, meta.player.posX);
    WriteFloat(buf, meta.player.posY);
    WriteFloat(buf, meta.player.posZ);
    WriteFloat(buf, meta.player.pitch);
    WriteFloat(buf, meta.player.yaw);
    WriteU8(buf, meta.player.selectedBlock);
    WriteI32(buf, meta.player.renderDistance);

    // Region index — all known regions
    WriteU32(buf, static_cast<uint32_t>(allRegions.size()));
    for (const auto& [key, r] : allRegions) {
        WriteI32(buf, r.rx);
        WriteI32(buf, r.rz);
        WriteI32(buf, r.chunkCount);
    }

    std::string worldDat = m_savePath + "/world.dat";
    return AtomicWriteFile(worldDat, buf);
}

// ── Load ─────────────────────────────────────────────────────────────

bool SaveManager::LoadWorld(WorldMeta& meta,
                            std::unordered_map<int64_t, std::array<uint8_t, 4096>>& chunkData) {
    if (!ReadWorldDat(meta)) return false;

    chunkData.clear();

    // Union indexed regions with a directory scan (index self-heals on save)
    std::unordered_map<int64_t, std::pair<int, int>> regions;
    for (const auto& r : meta.regions)
        regions[PackCoord(r.rx, 0, r.rz)] = {r.rx, r.rz};
    for (auto& [rx, rz] : ScanRegionDir())
        regions[PackCoord(rx, 0, rz)] = {rx, rz};

    for (const auto& [key, rc] : regions) {
        auto& [rx, rz] = rc;
        std::string regionPath =
            m_savePath + "/regions/" + RegionFile::RegionFileName(rx, rz);
        std::vector<ChunkSaveData> chunks;
        if (!RegionFile::Load(regionPath, chunks)) {
            SLEAK_WARN("World load: skipping region ({},{}) — unreadable {}",
                       rx, rz, regionPath);
            continue;
        }

        for (auto& c : chunks)
            chunkData[PackCoord(c.cx, c.cy, c.cz)] = c.blocks;
    }

    return true;
}

bool SaveManager::ReadWorldDat(WorldMeta& meta) const {
    std::string worldDat = m_savePath + "/world.dat";
    std::ifstream file(worldDat, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto fileSize = file.tellg();
    if (fileSize < 0) {
        SLEAK_WARN("World load: bad size for {}", worldDat);
        return false;
    }
    file.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(buf.data()), fileSize);
    if (!file.good()) return false;

    const uint8_t* p = buf.data();
    const uint8_t* end = p + buf.size();

    uint32_t magic;
    if (!ReadU32(p, end, magic) || magic != WorldMeta::MAGIC) return false;
    if (!ReadU16(p, end, meta.version) ||
        meta.version != WorldMeta::CURRENT_VERSION) {
        SLEAK_ERROR("World load: unsupported version in {}", worldDat);
        return false;
    }
    if (!ReadU16(p, end, meta.flags)) return false;
    if (!ReadI64(p, end, meta.saveTimestamp)) return false;
    if (!ReadString(p, end, meta.worldName)) return false;
    if (!ReadU32(p, end, meta.seed)) return false;

    // Player state
    if (!ReadFloat(p, end, meta.player.posX)) return false;
    if (!ReadFloat(p, end, meta.player.posY)) return false;
    if (!ReadFloat(p, end, meta.player.posZ)) return false;
    if (!ReadFloat(p, end, meta.player.pitch)) return false;
    if (!ReadFloat(p, end, meta.player.yaw)) return false;
    if (!ReadU8(p, end, meta.player.selectedBlock)) return false;
    if (!ReadI32(p, end, meta.player.renderDistance)) return false;

    // Region index
    uint32_t regionCount;
    if (!ReadU32(p, end, regionCount)) return false;
    // Bound count against remaining bytes (12-byte entry: rx, rz, chunkCount).
    if (regionCount > static_cast<size_t>(end - p) / 12) {
        SLEAK_ERROR("World load: regionCount {} exceeds file size in {}",
                    regionCount, worldDat);
        return false;
    }
    meta.regions.resize(regionCount);
    for (uint32_t i = 0; i < regionCount; ++i) {
        auto& r = meta.regions[i];
        if (!ReadI32(p, end, r.rx)) return false;
        if (!ReadI32(p, end, r.rz)) return false;
        if (!ReadI32(p, end, r.chunkCount)) return false;
    }

    return true;
}

// ── Static utility methods ───────────────────────────────────────────

std::vector<std::string> SaveManager::ListSaveDirectories(const std::string& basePath) {
    std::vector<std::string> dirs;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(basePath, ec)) return dirs;
    for (auto& entry : fs::directory_iterator(basePath, ec)) {
        if (entry.is_directory(ec)) {
            std::string worldDat = entry.path().string() + "/world.dat";
            if (fs::exists(worldDat, ec))
                dirs.push_back(entry.path().string());
        }
    }
    return dirs;
}

bool SaveManager::ReadWorldMetaOnly(const std::string& savePath, WorldMeta& meta) {
    SaveManager tmp;
    tmp.SetSavePath(savePath);
    return tmp.ReadWorldDat(meta);
}

bool SaveManager::DeleteSaveDirectory(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all(path, ec);
    return !ec;
}
