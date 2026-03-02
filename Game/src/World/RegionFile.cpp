#include "World/RegionFile.hpp"
#include <fstream>
#include <cstring>

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

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(buf.data()),
               static_cast<std::streamsize>(buf.size()));
    return file.good();
}

// ── Load ─────────────────────────────────────────────────────────────

bool RegionFile::Load(const std::string& path, std::vector<ChunkSaveData>& chunks) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto fileSize = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(buf.data()), fileSize);
    if (!file.good()) return false;

    const uint8_t* p = buf.data();
    const uint8_t* end = p + buf.size();

    uint32_t magic;
    uint16_t version, chunkCount;
    if (!ReadU32(p, end, magic) || magic != MAGIC) return false;
    if (!ReadU16(p, end, version) || version > CURRENT_VERSION) return false;
    if (!ReadU16(p, end, chunkCount)) return false;

    chunks.resize(chunkCount);
    for (uint16_t i = 0; i < chunkCount; ++i) {
        auto& c = chunks[i];
        if (!ReadI32(p, end, c.cx)) return false;
        if (!ReadI32(p, end, c.cy)) return false;
        if (!ReadI32(p, end, c.cz)) return false;

        uint32_t compSize, crc;
        if (!ReadU32(p, end, compSize)) return false;
        if (!ReadU32(p, end, crc)) return false;

        if (p + compSize > end) return false;
        if (!RLEDecode(p, compSize, c.blocks.data(), 4096)) return false;
        p += compSize;

        uint32_t checkCrc = CRC32(c.blocks.data(), 4096);
        if (checkCrc != crc) return false;
    }
    return true;
}
