#include "World/RegionFile.hpp"
#include <fstream>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <Logger.hpp>
#ifdef _WIN32
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

// ── Helpers ──────────────────────────────────────────────────────────

static void WriteU8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

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

// ── CRC32 ────────────────────────────────────────────────────────────

static uint32_t s_crcTable[256];
static bool s_crcInit = false;

static void InitCRCTable() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        s_crcTable[i] = crc;
    }
    s_crcInit = true;
}

uint32_t RegionFile::CRC32(const uint8_t* data, size_t size) {
    if (!s_crcInit) InitCRCTable();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; ++i)
        crc = (crc >> 8) ^ s_crcTable[(crc ^ data[i]) & 0xFF];
    return crc ^ 0xFFFFFFFF;
}

// ── RLE ──────────────────────────────────────────────────────────────

std::vector<uint8_t> RegionFile::RLEEncode(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < size) {
        uint8_t val = data[i];
        uint16_t count = 1;
        while (i + count < size && data[i + count] == val && count < 65535)
            ++count;
        WriteU16(out, count);
        WriteU8(out, val);
        i += count;
    }
    return out;
}

bool RegionFile::RLEDecode(const uint8_t* encoded, size_t encodedSize,
                           uint8_t* output, size_t expectedSize) {
    const uint8_t* p = encoded;
    const uint8_t* end = encoded + encodedSize;
    size_t written = 0;
    while (p < end) {
        uint16_t count;
        uint8_t val;
        if (!ReadU16(p, end, count)) return false;
        if (!ReadU8(p, end, val)) return false;
        if (written + count > expectedSize) return false;
        std::memset(output + written, val, count);
        written += count;
    }
    return written == expectedSize;
}

// ── Region coord ─────────────────────────────────────────────────────

void RegionFile::RegionCoord(int cx, int cz, int& rx, int& rz) {
    // Floor division by REGION_SIZE
    rx = (cx >= 0) ? cx / REGION_SIZE : (cx - REGION_SIZE + 1) / REGION_SIZE;
    rz = (cz >= 0) ? cz / REGION_SIZE : (cz - REGION_SIZE + 1) / REGION_SIZE;
}

std::string RegionFile::RegionFileName(int rx, int rz) {
    return "r." + std::to_string(rx) + "." + std::to_string(rz) + ".dat";
}

// ── Save ─────────────────────────────────────────────────────────────

bool RegionFile::Save(const std::string& path, const std::vector<ChunkSaveData>& chunks) {
    std::vector<uint8_t> buf;

    WriteU32(buf, MAGIC);
    WriteU16(buf, CURRENT_VERSION);
    WriteU16(buf, static_cast<uint16_t>(chunks.size()));

    for (const auto& c : chunks) {
        WriteI32(buf, c.cx);
        WriteI32(buf, c.cy);
        WriteI32(buf, c.cz);

        auto compressed = RLEEncode(c.blocks.data(), c.blocks.size());
        uint32_t crc = CRC32(c.blocks.data(), c.blocks.size());

        WriteU32(buf, static_cast<uint32_t>(compressed.size()));
        WriteU32(buf, crc);
        buf.insert(buf.end(), compressed.begin(), compressed.end());
    }

    return AtomicWriteFile(path, buf);
}

// ── Load ─────────────────────────────────────────────────────────────

bool RegionFile::Load(const std::string& path, std::vector<ChunkSaveData>& chunks,
                      size_t* droppedChunks) {
    if (droppedChunks) *droppedChunks = 0;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto fileSize = file.tellg();
    if (fileSize < 0) {
        SLEAK_WARN("Region load: bad size for {}", path);
        return false;
    }
    file.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(buf.data()), fileSize);
    if (!file.good()) {
        SLEAK_WARN("Region load: short read for {}", path);
        return false;
    }

    const uint8_t* p = buf.data();
    const uint8_t* end = p + buf.size();

    uint32_t magic;
    uint16_t version, chunkCount;
    if (!ReadU32(p, end, magic) || magic != MAGIC) {
        SLEAK_WARN("Region load: bad magic in {}", path);
        return false;
    }
    if (!ReadU16(p, end, version) || version != CURRENT_VERSION) {
        SLEAK_ERROR("Region load: unsupported version in {}", path);
        return false;
    }
    if (!ReadU16(p, end, chunkCount)) return false;

    // Bound count against remaining bytes (min 20-byte header per chunk).
    size_t remaining = static_cast<size_t>(end - p);
    if (chunkCount > remaining / 20) {
        SLEAK_ERROR("Region load: chunkCount {} exceeds file size in {}",
                    chunkCount, path);
        return false;
    }

    chunks.clear();
    chunks.reserve(chunkCount);
    for (uint16_t i = 0; i < chunkCount; ++i) {
        ChunkSaveData c;
        if (!ReadI32(p, end, c.cx) || !ReadI32(p, end, c.cy) ||
            !ReadI32(p, end, c.cz)) {
            SLEAK_WARN("Region load: truncated header in {}", path);
            return false;  // structural truncation — stop
        }

        uint32_t compSize, crc;
        if (!ReadU32(p, end, compSize) || !ReadU32(p, end, crc)) {
            SLEAK_WARN("Region load: truncated chunk record in {}", path);
            return false;
        }
        if (p + compSize > end) {
            SLEAK_WARN("Region load: chunk payload overruns {}", path);
            return false;
        }

        // Per-chunk recovery: a bad decode/CRC drops one chunk, not the region.
        bool decoded = RLEDecode(p, compSize, c.blocks.data(), 4096);
        p += compSize;
        if (!decoded) {
            SLEAK_WARN("Region load: RLE decode failed for chunk ({},{},{}) in {}",
                       c.cx, c.cy, c.cz, path);
            if (droppedChunks) ++*droppedChunks;
            continue;
        }
        if (CRC32(c.blocks.data(), 4096) != crc) {
            SLEAK_WARN("Region load: CRC mismatch for chunk ({},{},{}) in {}",
                       c.cx, c.cy, c.cz, path);
            if (droppedChunks) ++*droppedChunks;
            continue;
        }
        chunks.push_back(std::move(c));
    }
    return true;
}
