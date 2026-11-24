#include "World/ChunkVisibilityGraph.hpp"

#include <algorithm>
#include <cstdlib>

namespace {

constexpr int SIZE = Chunk::SIZE;
// Cells in one horizontal layer of a chunk
constexpr int LAYER = SIZE * SIZE;

// Grid layer = chunk Y
static_assert(WorldGenerator::MIN_CHUNK_Y == 0);
// Reached packs a column's chunks into a byte
static_assert(ChunkVisibilityGraph::WORLD_LAYERS <= 8);

/// Step of each BlockFace direction in x, y, z.
constexpr int STEP[BLOCK_FACE_COUNT][3] = {{0, 1, 0},  {0, -1, 0}, {0, 0, 1},
                                           {0, 0, -1}, {1, 0, 0},  {-1, 0, 0}};

/// The BlockFace opposite a face: pairs differ in the low bit.
constexpr int Opposite(int face) { return face ^ 1; }
static_assert(Opposite(static_cast<int>(BlockFace::East)) ==
              static_cast<int>(BlockFace::West));

/// Bit of a BlockFace in a face set.
constexpr uint8_t FaceBit(BlockFace face) {
    return static_cast<uint8_t>(1u << static_cast<int>(face));
}

/// Faces of the chunk a cell lies on.
uint8_t CellFaces(int x, int y, int z) {
    uint8_t faces = 0;
    if (y == SIZE - 1) faces |= FaceBit(BlockFace::Top);
    if (y == 0) faces |= FaceBit(BlockFace::Bottom);
    if (z == SIZE - 1) faces |= FaceBit(BlockFace::North);
    if (z == 0) faces |= FaceBit(BlockFace::South);
    if (x == SIZE - 1) faces |= FaceBit(BlockFace::East);
    if (x == 0) faces |= FaceBit(BlockFace::West);
    return faces;
}

}  // namespace

uint16_t FaceConnectivity(const ChunkCells& open) {
    if (open.none()) return 0;
    if (open.all()) return FULL_FACE_MASK;
    ChunkCells seen;
    uint16_t stack[Chunk::VOLUME];
    uint16_t mask = 0;
    for (int seed = 0; seed < Chunk::VOLUME && mask != FULL_FACE_MASK; ++seed) {
        // Only components that touch a face can connect faces
        if (!open[seed] || seen[seed] ||
            CellFaces(seed % SIZE, seed / LAYER, seed / SIZE % SIZE) == 0)
            continue;
        uint8_t faces = 0;
        int top = 0;
        seen.set(seed);
        stack[top++] = static_cast<uint16_t>(seed);
        while (top > 0) {
            const int i = stack[--top];
            const int x = i % SIZE, z = i / SIZE % SIZE, y = i / LAYER;
            faces |= CellFaces(x, y, z);
            auto visit = [&](bool inside, int next) {
                if (!inside || !open[next] || seen[next]) return;
                seen.set(next);
                stack[top++] = static_cast<uint16_t>(next);
            };
            visit(x > 0, i - 1);
            visit(x < SIZE - 1, i + 1);
            visit(z > 0, i - SIZE);
            visit(z < SIZE - 1, i + SIZE);
            visit(y > 0, i - LAYER);
            visit(y < SIZE - 1, i + LAYER);
        }
        for (int a = 0; a < BLOCK_FACE_COUNT; ++a)
            for (int b = a + 1; b < BLOCK_FACE_COUNT; ++b)
                if ((faces >> a & 1) && (faces >> b & 1))
                    mask |= static_cast<uint16_t>(1u << FacePairBit(a, b));
    }
    return mask;
}

Chunk::Visibility ComputeChunkVisibility(const uint8_t* blocks) {
    Chunk::Visibility vis;
    ChunkCells seeThrough, coveredOpen, coveredClosed;
    bool coveredWater = false;  // water under an opaque cell
    bool passWater = false;     // water with only water above it
    for (int z = 0; z < SIZE; ++z) {
        for (int x = 0; x < SIZE; ++x) {
            // First non-water cell above each cell, walking down
            enum class Above { Unknown, Open, Closed } above = Above::Unknown;
            for (int y = SIZE - 1; y >= 0; --y) {
                const int i = x + z * SIZE + y * LAYER;
                const auto type = static_cast<BlockType>(blocks[i]);
                if (IsBlockWater(type)) {
                    seeThrough.set(i);
                    if (above == Above::Open) {
                        coveredOpen.set(i);
                        coveredClosed.set(i);
                        vis.openWater = true;
                    } else if (above == Above::Unknown) {
                        coveredOpen.set(i);
                        passWater = true;
                    } else {
                        coveredWater = true;
                    }
                } else if (IsBlockOpaque(type)) {
                    above = Above::Closed;
                } else {
                    above = Above::Open;
                    seeThrough.set(i);
                    coveredOpen.set(i);
                    coveredClosed.set(i);
                }
            }
            const int column = x + z * SIZE;
            vis.waterTop[column] = IsBlockWater(
                static_cast<BlockType>(blocks[column + (SIZE - 1) * LAYER]));
            vis.downOpen[column] = above == Above::Open;
            vis.downClosed[column] = above == Above::Closed;
        }
    }
    // Equal cell sets skip their flood fill
    vis.seeThrough = FaceConnectivity(seeThrough);
    vis.coveredOpen =
        coveredWater ? FaceConnectivity(coveredOpen) : vis.seeThrough;
    vis.coveredClosed =
        passWater ? FaceConnectivity(coveredClosed) : vis.coveredOpen;
    return vis;
}

uint16_t ResolveCoveredWater(const Chunk::Visibility& vis,
                             Chunk::Columns& openAbove, bool& openWater) {
    const bool anyOpen = (vis.waterTop & openAbove).any();
    const bool anyClosed = (vis.waterTop & ~openAbove).any();
    openWater = vis.openWater || anyOpen;
    // Columns that are all water pass the state above them on
    openAbove = (openAbove & ~(vis.downOpen | vis.downClosed)) | vis.downOpen;
    // Mixed inputs take the open variant, a superset of the true links
    return anyClosed && !anyOpen ? vis.coveredClosed : vis.coveredOpen;
}

void ChunkVisibilityGraph::Reset(int centerX, int centerZ, int radius) {
    m_centerX = centerX;
    m_centerZ = centerZ;
    m_radius = radius;
    m_width = 2 * radius + 1;
    const size_t count = static_cast<size_t>(m_width) * m_width * LAYERS;
    m_cells.assign(count, Cell{});
    m_reached.assign(count, 0);
}

ChunkVisibilityGraph::Cell* ChunkVisibilityGraph::At(int cx, int cy, int cz) {
    const int lx = cx - m_centerX + m_radius;
    const int lz = cz - m_centerZ + m_radius;
    if (lx < 0 || lx >= m_width || lz < 0 || lz >= m_width || cy < 0 ||
        cy >= LAYERS)
        return nullptr;
    return &m_cells[Index(lx, cy, lz)];
}

uint16_t ChunkVisibilityGraph::MaskAt(int index, bool waterCamera) const {
    const Cell& cell = m_cells[index];
    if (waterCamera || cell.openWater) return cell.seeThrough;
    // Covered water within one chunk of open water counts as open
    const int lx = index % m_width;
    const int lz = index / m_width % m_width;
    const int y = index / (m_width * m_width);
    for (const int* step : STEP) {
        const int nx = lx + step[0], ny = y + step[1], nz = lz + step[2];
        if (nx < 0 || nx >= m_width || nz < 0 || nz >= m_width || ny < 0 ||
            ny >= LAYERS)
            continue;
        if (m_cells[Index(nx, ny, nz)].openWater) return cell.seeThrough;
    }
    return cell.covered;
}

void ChunkVisibilityGraph::Run(int cameraChunkY, bool waterCamera,
                               float reachSq) {
    const int startY = std::clamp(cameraChunkY, 0, LAYERS - 1);

    // Columns within reach of the nearest point of the camera's column: the
    // gap between the two footprints, a whole number of chunks per axis
    m_inReach.assign(static_cast<size_t>(m_width) * m_width, 0);
    for (int lz = 0; lz < m_width; ++lz) {
        for (int lx = 0; lx < m_width; ++lx) {
            const float gx = static_cast<float>(
                std::max(std::abs(lx - m_radius) - 1, 0) * SIZE);
            const float gz = static_cast<float>(
                std::max(std::abs(lz - m_radius) - 1, 0) * SIZE);
            m_inReach[lx + lz * m_width] = gx * gx + gz * gz <= reachSq;
        }
    }

    m_sources.assign(m_cells.size(), 0);
    m_reached.assign(m_cells.size(), 0);
    m_queue.clear();
    const int start = Index(m_radius, startY, m_radius);
    m_reached[start] = 1;
    m_queue.push_back(start);
    for (size_t head = 0; head < m_queue.size(); ++head) {
        const int node = m_queue[head];
        const int lx = node % m_width;
        const int lz = node / m_width % m_width;
        const int y = node / (m_width * m_width);
        const int offset[3] = {lx - m_radius, y - startY, lz - m_radius};
        const uint16_t mask =
            node == start ? FULL_FACE_MASK : MaskAt(node, waterCamera);
        for (int dir = 0; dir < BLOCK_FACE_COUNT; ++dir) {
            const int* step = STEP[dir];
            // Away from the camera only: never back along an axis walked
            if (offset[0] * step[0] < 0 || offset[1] * step[1] < 0 ||
                offset[2] * step[2] < 0)
                continue;
            // Leave through a face the chunk links to an entry face
            if (node != start) {
                bool linked = false;
                for (int from = 0; from < BLOCK_FACE_COUNT && !linked; ++from)
                    linked = (m_sources[node] >> from & 1) &&
                             FacesConnect(mask, Opposite(from), dir);
                if (!linked) continue;
            }
            const int nx = lx + step[0], ny = y + step[1], nz = lz + step[2];
            if (nx < 0 || nx >= m_width || nz < 0 || nz >= m_width || ny < 0 ||
                ny >= LAYERS || !m_inReach[nx + nz * m_width])
                continue;
            const int next = Index(nx, ny, nz);
            m_sources[next] |= static_cast<uint8_t>(1u << dir);
            if (m_reached[next]) continue;
            m_reached[next] = 1;
            m_queue.push_back(next);
        }
    }
}

uint8_t ChunkVisibilityGraph::Reached(int cx, int cz) const {
    const int lx = cx - m_centerX + m_radius;
    const int lz = cz - m_centerZ + m_radius;
    if (lx < 0 || lx >= m_width || lz < 0 || lz >= m_width) return 0;
    uint8_t chunks = 0;
    for (int y = 0; y < WORLD_LAYERS; ++y)
        if (m_reached[Index(lx, y, lz)])
            chunks |= static_cast<uint8_t>(1u << y);
    return chunks;
}
