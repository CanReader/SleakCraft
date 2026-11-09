#ifndef _BLOCK_EFFECTS_HPP_
#define _BLOCK_EFFECTS_HPP_

#include <Math/Vector.hpp>
#include <Memory/RefPtr.hpp>
#include <Runtime/MeshBatch.hpp>
#include <cstdint>
#include <vector>

#include "Block.hpp"
#include "VoxelVertex.hpp"

namespace Sleak {
class Material;
}

/// A block's scale-in placement animation, tracked until its duration elapses.
struct PlaceEffect {
    int x, y, z;
    BlockType type;
    float timer;
    float duration;
    bool done = false;
};

/// One gravity-affected debris quad from a broken block, facing the camera.
struct BreakParticle {
    Sleak::Math::Vector3D pos;
    Sleak::Math::Vector3D vel;
    float life;
    float maxLife;
    uint8_t tile;
};

/// Purely visual block place/break effects (scale-in cube, debris particles),
/// drawn in the voxel vertex format so they get the voxel GBuffer, lighting
/// and shadows. The voxel edit happens once a placement's animation
/// completes (see PopCompletedPlacements).
/// @ingroup world
class BlockEffects {
   public:
    /// Keeps the block material the effects draw with.
    void Initialize(const Sleak::RefPtr<Sleak::Material>& material);

    /// Spawns a scale-in cube at a block position; the real SetBlockAt call
    /// is deferred until this effect completes.
    void SpawnPlaceEffect(int x, int y, int z, BlockType type);
    /// Spawns a burst of camera-facing debris quads at a broken block.
    void SpawnBreakEffect(int x, int y, int z, BlockType type);

    /// Advances place-effect scale and particle physics, then rebuilds the
    /// effect mesh for this frame.
    void Update(float deltaTime, const Sleak::Math::Vector3D& cameraPos);
    /// Submits this frame's effect mesh with the block material.
    void Render() const;
    /// Drops every effect without committing placements.
    void Cleanup();

    struct CompletedPlace {
        int x, y, z;
        BlockType type;
    };
    /// Removes and returns placements whose animation finished this frame.
    std::vector<CompletedPlace> PopCompletedPlacements();
    /// Force-completes every still-animating placement; used by save/exit
    /// paths so no in-flight placement is lost.
    std::vector<CompletedPlace> DrainAllPlacements();

   private:
    /// Appends the scaled, textured cube of a running place effect.
    void AppendPlaceCube(const PlaceEffect& effect);
    /// Appends a two-sided quad facing the camera for a break particle.
    void AppendParticle(const BreakParticle& particle,
                        const Sleak::Math::Vector3D& cameraPos);
    /// Uploads the effects of this frame, or releases the mesh when idle.
    void RebuildMesh(const Sleak::Math::Vector3D& cameraPos);

    Sleak::RefPtr<Sleak::Material> m_material;

    std::vector<PlaceEffect> m_placeEffects;
    std::vector<BreakParticle> m_breakParticles;

    VoxelVertexBuffer m_vertices;
    std::vector<uint32_t> m_indices;
    Sleak::MeshHandle m_mesh;

    static constexpr float PLACE_DURATION = 0.15f;
    static constexpr float PLACE_START_SCALE = 0.3f;
    static constexpr float BREAK_LIFETIME = 0.6f;
    static constexpr float BREAK_LIFETIME_JITTER = 0.1f;
    static constexpr float BREAK_SPREAD = 0.3f;
    static constexpr float BREAK_SIDE_SPEED = 1.5f;
    static constexpr float BREAK_UP_SPEED_MIN = 1.5f;
    static constexpr float BREAK_UP_SPEED_MAX = 4.0f;
    static constexpr float PARTICLE_SIZE = 0.1f;
    static constexpr int PARTICLES_PER_BLOCK = 8;
    static constexpr float PARTICLE_GRAVITY = -12.0f;
};

#endif
