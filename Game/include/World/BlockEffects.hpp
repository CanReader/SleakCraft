#ifndef _BLOCK_EFFECTS_HPP_
#define _BLOCK_EFFECTS_HPP_

#include "Block.hpp"
#include <Math/Vector.hpp>
#include <Memory/RefPtr.hpp>
#include <vector>

namespace Sleak {
    class Material;
    class GameObject;
    class SceneBase;
}

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
/// Owns their scene objects; the actual voxel edit happens separately once
/// a placement's animation completes (see PopCompletedPlacements).
class BlockEffects {
public:
    void Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material);

    /// Spawns a scale-in cube at a block position; the real SetBlockAt call
    /// is deferred until this effect completes.
    void SpawnPlaceEffect(int x, int y, int z, BlockType type);
    /// Spawns a burst of camera-facing debris quads at a broken block.
    void SpawnBreakEffect(int x, int y, int z, BlockType type);

    /// Advances place-effect scale and particle physics, removing finished ones.
    void Update(float deltaTime, const Sleak::Math::Vector3D& cameraPos);
    void Cleanup();

    struct CompletedPlace { int x, y, z; BlockType type; };
    /// Removes and returns placements whose animation finished this frame.
    std::vector<CompletedPlace> PopCompletedPlacements();
    /// Force-completes every still-animating placement; used by save/exit
    /// paths so no in-flight placement is lost.
    std::vector<CompletedPlace> DrainAllPlacements();


private:
    /// Builds a textured unit-cube GameObject for a place effect.
    Sleak::GameObject* CreatePlaceCube(BlockType type);
    /// Builds a single-tile textured quad GameObject for a break particle.
    Sleak::GameObject* CreateParticleQuad(uint8_t tileIndex);

    Sleak::SceneBase* m_scene = nullptr;
    Sleak::RefPtr<Sleak::Material> m_material;

    std::vector<PlaceEffect> m_placeEffects;
    std::vector<BreakParticle> m_breakParticles;

    static constexpr float PLACE_DURATION = 0.15f;
    static constexpr float BREAK_LIFETIME = 0.6f;
    static constexpr float PARTICLE_SIZE = 0.1f;
    static constexpr int PARTICLES_PER_BLOCK = 8;
    static constexpr float PARTICLE_GRAVITY = -12.0f;
};

#endif
