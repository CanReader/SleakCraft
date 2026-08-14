#include "UI/Hotbar.hpp"

#include "World/Block.hpp"
#include <UI/UI.hpp>

using namespace Sleak;

// Helper: get the representative texture path for a block type (front/side
// face)
static const char* GetBlockTexturePath(BlockType type) {
    switch (type) {
        case BlockType::Grass:
            return "assets/textures/blocks/grass_block_side.png";
        case BlockType::Dirt:
            return "assets/textures/blocks/dirt.png";
        case BlockType::Stone:
            return "assets/textures/blocks/stone.png";
        case BlockType::Cobblestone:
            return "assets/textures/blocks/cobblestone.png";
        case BlockType::OakLog:
            return "assets/textures/blocks/oak_log.png";
        case BlockType::DarkOakLog:
            return "assets/textures/blocks/dark_oak_log.png";
        case BlockType::SpruceLog:
            return "assets/textures/blocks/spruce_log.png";
        case BlockType::OakPlanks:
            return "assets/textures/blocks/oak_planks.png";
        case BlockType::Bricks:
            return "assets/textures/blocks/brick.png";
        case BlockType::Sand:
            return "assets/textures/blocks/sand.png";
        case BlockType::Gravel:
            return "assets/textures/blocks/gravel.png";
        case BlockType::OakLeaves:
            return "assets/textures/blocks/oak_leaves.png";
        default:
            return "assets/textures/blocks/stone.png";
    }
}

void Hotbar::Render() {
    // Lazy-load block textures for UI display
    if (!m_hotbarTexturesLoaded) {
        for (int i = 0; i < BlockInteraction::HOTBAR_SLOTS; i++)
            m_hotbarTextures[i] = UI::LoadTextureForUI(
                GetBlockTexturePath(m_blockInteraction.GetHotbarSlot(i)));
        m_hotbarTexturesLoaded = true;
    }

    constexpr float slotSize = 48.0f;
    constexpr float slotPadding = 4.0f;
    constexpr float iconPadding = 4.0f;
    constexpr float borderWidth = 2.0f;
    constexpr float bottomMargin = 20.0f;

    float totalWidth = BlockInteraction::HOTBAR_SLOTS * slotSize +
                       (BlockInteraction::HOTBAR_SLOTS - 1) * slotPadding;
    float startX = (UI::GetViewportWidth() - totalWidth) * 0.5f;
    float startY = UI::GetViewportHeight() - slotSize - bottomMargin;

    // Background bar
    UI::DrawFilledRect(startX - 6.0f, startY - 6.0f, totalWidth + 12.0f,
                       slotSize + 12.0f, 0.0f, 0.0f, 0.0f, 0.45f, 6.0f);

    for (int i = 0; i < BlockInteraction::HOTBAR_SLOTS; i++) {
        float x = startX + i * (slotSize + slotPadding);
        float y = startY;

        bool selected = (i == m_blockInteraction.GetSelectedSlot());

        // Slot background
        if (selected) {
            UI::DrawFilledRect(x, y, slotSize, slotSize, 1.0f, 1.0f, 1.0f,
                               0.25f, 4.0f);
        } else {
            UI::DrawFilledRect(x, y, slotSize, slotSize, 0.2f, 0.2f, 0.2f, 0.5f,
                               4.0f);
        }

        // Block texture
        if (m_hotbarTextures[i] != 0) {
            UI::DrawImage(m_hotbarTextures[i], x + iconPadding, y + iconPadding,
                          slotSize - iconPadding * 2,
                          slotSize - iconPadding * 2);
        }

        // Selection border
        if (selected) {
            UI::DrawRect(x - 1.0f, y - 1.0f, slotSize + 2.0f, slotSize + 2.0f,
                         1.0f, 1.0f, 1.0f, 0.9f, borderWidth, 4.0f);
        } else {
            UI::DrawRect(x, y, slotSize, slotSize, 0.5f, 0.5f, 0.5f, 0.3f, 1.0f,
                         4.0f);
        }

        // Slot number
        char num[2] = {static_cast<char>('1' + i), '\0'};
        UI::DrawText(num, x + 3.0f, y + 1.0f, 0.7f, 0.7f, 0.7f, 0.7f);
    }
}
