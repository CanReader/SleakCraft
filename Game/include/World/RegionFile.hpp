#ifndef _REGION_FILE_HPP_
#define _REGION_FILE_HPP_

#include <cstdint>
#include <vector>
#include <string>
#include <array>

struct ChunkSaveData {
    int32_t cx, cy, cz;
    std::array<uint8_t, 4096> blocks;
};

class RegionFile {
public:
    static constexpr uint32_t MAGIC = 0x534C4B52; // "SLKR"
    static constexpr uint16_t CURRENT_VERSION = 1;
    static constexpr int REGION_SIZE = 8;

    static void RegionCoord(int cx, int cz, int& rx, int& rz);
    static std::string RegionFileName(int rx, int rz);

    static bool Save(const std::string& path, const std::vector<ChunkSaveData>& chunks);
    static bool Load(const std::string& path, std::vector<ChunkSaveData>& chunks);

    static std::vector<uint8_t> RLEEncode(const uint8_t* data, size_t size);
    static bool RLEDecode(const uint8_t* encoded, size_t encodedSize,
                          uint8_t* output, size_t expectedSize);

    static uint32_t CRC32(const uint8_t* data, size_t size);
};

#endif
