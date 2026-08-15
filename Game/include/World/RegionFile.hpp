#ifndef _REGION_FILE_HPP_
#define _REGION_FILE_HPP_

#include <cstdint>
#include <vector>
#include <string>
#include <array>

/// One chunk's raw block array plus its coordinate, as stored in a region file.
struct ChunkSaveData {
    int32_t cx, cy, cz;
    std::array<uint8_t, 4096> blocks;
};

/// Binary on-disk format grouping REGION_SIZE^2 chunk columns into one file:
/// per-chunk RLE compression, a CRC32 per chunk, and an atomic (temp file +
/// fsync + rename) write so a crash mid-save can't corrupt an existing file.
/// @ingroup persistence
class RegionFile {
public:
    static constexpr uint32_t MAGIC = 0x534C4B52; // "SLKR"
    static constexpr uint16_t CURRENT_VERSION = 1;
    static constexpr int REGION_SIZE = 8;

    /// Floor-divides a chunk coordinate into its region coordinate.
    static void RegionCoord(int cx, int cz, int& rx, int& rz);
    static std::string RegionFileName(int rx, int rz);

    /// Serializes and atomically writes all given chunks to one region file.
    static bool Save(const std::string& path, const std::vector<ChunkSaveData>& chunks);
    /// Reads a region file, dropping (and counting via droppedChunks) any
    /// chunk whose RLE payload fails to decode or fails its CRC check;
    /// returns false only on structural corruption of the file itself.
    static bool Load(const std::string& path, std::vector<ChunkSaveData>& chunks,
                     size_t* droppedChunks = nullptr);

    /// Run-length encodes a byte buffer as (count:u16, value:u8) pairs.
    static std::vector<uint8_t> RLEEncode(const uint8_t* data, size_t size);
    /// Inverse of RLEEncode; fails if the decoded length doesn't match
    /// expectedSize or the input is truncated.
    static bool RLEDecode(const uint8_t* encoded, size_t encodedSize,
                          uint8_t* output, size_t expectedSize);

    static uint32_t CRC32(const uint8_t* data, size_t size);
};

#endif
