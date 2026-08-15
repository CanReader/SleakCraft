#ifndef _HOTBAR_HPP_
#define _HOTBAR_HPP_

#include "Player/BlockInteraction.hpp"
#include <array>
#include <cstdint>

/// Bottom-center hotbar strip: block icon texture cache + slot rendering.
/// Selection/slot state lives on BlockInteraction and is read via reference.
/// @ingroup ui
class Hotbar {
public:
    explicit Hotbar(BlockInteraction& blockInteraction)
        : m_blockInteraction(blockInteraction) {}

    /// Lazy-loads slot icon textures on first call, then draws the strip.
    void Render();

private:
    BlockInteraction& m_blockInteraction;
    std::array<uint64_t, BlockInteraction::HOTBAR_SLOTS> m_hotbarTextures =
        {};
    bool m_hotbarTexturesLoaded = false;
};

#endif
