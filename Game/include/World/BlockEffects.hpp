#ifndef _BLOCK_EFFECTS_HPP_
#define _BLOCK_EFFECTS_HPP_

#include <Math/Vector.hpp>
#include <Memory/RefPtr.hpp>
#include <Runtime/MeshBatch.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Block.hpp"

namespace Sleak {
class Material;
class GameObject;
class SceneBase;
}  // namespace Sleak

/// A block's scale-in placement animation, tracked until its duration elapses.
struct PlaceEffect {
    int x, y, z;
    BlockType type;
    float timer;
    float duration;
    Sleak::GameObject* obj = nullptr;
};

/// One gravity-affected debris quad from a broken block, facing the camera.
struct BreakParticle {
    Sleak::Math::Vector3D pos;
    Sleak::Math::Vector3D vel;
    float life;
    float maxLife;
    Sleak::GameObject* obj = nullptr;
};

/// Purely visual block place/break effects (scale-in cube, debris particles).
/// Owns their scene objects, which draw voxel-format meshes built once per
/// block type or tile, so they get the voxel GBuffer, lighting and shadows.
/// The voxel edit happens once a placement's animation completes (see
/// PopCompletedPlacements).
/// @ingroup world
class BlockEffects {
   public:
    /// Keeps the scene the effect objects join and the block material.
    void Initialize(Sleak::SceneBase* scene,
                    const Sleak::RefPtr<Sleak::Material>& material);

    /// Spawns a scale-in cube at a block position; the real SetBlockAt call
    /// is deferred until this effect completes.
    void SpawnPlaceEffect(int x, int y, int z, BlockType type);
    /// Spawns a burst of camera-facing debris quads at a broken block.
    void SpawnBreakEffect(int x, int y, int z, BlockType type);

    /// Advances place-effect scale and particle physics, removing finished
    /// ones.
    void Update(float deltaTime, const Sleak::Math::Vector3D& cameraPos);
    /// Removes every effect object without committing placements.
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
    /// Unit cube of a block type in the voxel format, built on first use.
    const Sleak::MeshHandle& CubeMesh(BlockType type);
    /// Two-sided quad of an atlas tile in the voxel format, built on first
    /// use.
    const Sleak::MeshHandle& QuadMesh(uint8_t tile);
    /// Scene-ready object drawing a mesh with the block material.
    Sleak::GameObject* CreateEffectObject(const char* name,
                                          const Sleak::MeshHandle& mesh);

    Sleak::SceneBase* m_scene = nullptr;
    Sleak::RefPtr<Sleak::Material> m_material;

    std::vector<PlaceEffect> m_placeEffects;
    std::vector<BreakParticle> m_breakParticles;

    // Shared by every effect object of a type or tile
    std::unordered_map<BlockType, Sleak::MeshHandle> m_cubeMeshes;
    std::unordered_map<uint8_t, Sleak::MeshHandle> m_quadMeshes;

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
