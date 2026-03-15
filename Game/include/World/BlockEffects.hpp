#ifndef _BLOCK_EFFECTS_HPP_
#define _BLOCK_EFFECTS_HPP_

#include "Block.hpp"
#include <Math/Vector.hpp>
#include <Memory/RefPtr.h>
#include <vector>

namespace Sleak {
    class Material;
    class GameObject;
    class SceneBase;
}

struct PlaceEffect {
    int x, y, z;
    BlockType type;
    float timer;
    float duration;
    Sleak::GameObject* obj = nullptr;
};

struct BreakParticle {
    Sleak::Math::Vector3D pos;
    Sleak::Math::Vector3D vel;
    float life;
    float maxLife;
    Sleak::GameObject* obj = nullptr;
};

class BlockEffects {
public:
    void Initialize(Sleak::SceneBase* scene, const Sleak::RefPtr<Sleak::Material>& material);

    void SpawnPlaceEffect(int x, int y, int z, BlockType type);
    void SpawnBreakEffect(int x, int y, int z, BlockType type);

    void Update(float deltaTime);
    void Cleanup();

    struct CompletedPlace { int x, y, z; BlockType type; };
    std::vector<CompletedPlace> PopCompletedPlacements();

private:
    Sleak::GameObject* CreatePlaceCube(BlockType type);
    Sleak::GameObject* CreateParticleQuad(uint8_t tileIndex);

    Sleak::SceneBase* m_scene = nullptr;
    Sleak::RefPtr<Sleak::Material> m_material;

    std::vector<PlaceEffect> m_placeEffects;
    std::vector<BreakParticle> m_breakParticles;

    static constexpr float PLACE_DURATION = 0.15f;
    static constexpr float BREAK_LIFETIME = 0.6f;
    static constexpr float PARTICLE_SIZE = 0.2f;
    static constexpr int PARTICLES_PER_BLOCK = 8;
    static constexpr float PARTICLE_GRAVITY = -12.0f;
};

#endif
