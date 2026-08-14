#ifndef _BLOCK_INTERACTION_HPP_
#define _BLOCK_INTERACTION_HPP_

#include "World/Block.hpp"
#include <Events/KeyboardEvent.hpp>
#include <Events/MouseEvent.hpp>
#include <array>

class ChunkManager;
class BlockEffects;
class MainScene;

/// Block break/place raycast, the block outline, hotbar slot state + scroll
/// cycling + number-key selection, and draining BlockEffects' completed
/// placements into the world. Camera is fetched fresh via the owning
/// MainScene where needed.
class BlockInteraction {
public:
    static constexpr int HOTBAR_SLOTS = 9;

    BlockInteraction(ChunkManager& chunkManager, BlockEffects& blockEffects,
                      MainScene& scene)
        : m_chunkManager(chunkManager),
          m_blockEffects(blockEffects),
          m_scene(scene) {}

    /// Raycasts from the camera and breaks or places a block on click.
    void OnMousePressed(
        const Sleak::Events::Input::MouseButtonPressedEvent& e);
    /// Cycles the selected hotbar slot.
    void OnMouseScrolled(const Sleak::Events::Input::MouseScrolledEvent& e);
    /// Number-key hotbar slot selection.
    void OnKeyPressed(const Sleak::Events::Input::KeyPressedEvent& e);
    /// Draws a wireframe box around the block the camera is looking at.
    void RenderOutline();
    /// Applies BlockEffects placements whose place animation has finished.
    void DrainCompletedPlacements();

    BlockType GetSelectedBlock() const { return m_selectedBlock; }
    void SetSelectedBlock(BlockType type) { m_selectedBlock = type; }
    int GetSelectedSlot() const { return m_selectedSlot; }
    BlockType GetHotbarSlot(int i) const { return m_hotbar[i]; }

private:
    ChunkManager& m_chunkManager;
    BlockEffects& m_blockEffects;
    MainScene& m_scene;

    BlockType m_selectedBlock = BlockType::Grass;
    int m_selectedSlot = 0;
    std::array<BlockType, HOTBAR_SLOTS> m_hotbar = {
        {BlockType::Grass, BlockType::Dirt, BlockType::Stone,
         BlockType::Cobblestone, BlockType::OakLog, BlockType::DarkOakLog,
         BlockType::SpruceLog, BlockType::OakPlanks, BlockType::Bricks}};
};

#endif
