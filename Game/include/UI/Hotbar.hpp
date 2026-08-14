#ifndef _HOTBAR_HPP_
#define _HOTBAR_HPP_

#include "Player/BlockInteraction.hpp"
#include <array>
#include <cstdint>

// Bottom-center hotbar strip: block icon texture cache + slot rendering.
// Selection/slot state lives on BlockInteraction and is read via reference.
class Hotbar {
public:
    explicit Hotbar(BlockInteraction& blockInteraction)
        : m_blockInteraction(blockInteraction) {}

    void Render();

private:
    BlockInteraction& m_blockInteraction;
    std::array<uint64_t, BlockInteraction::HOTBAR_SLOTS> m_hotbarTextures =
        {};
    bool m_hotbarTexturesLoaded = false;
};

#endif
