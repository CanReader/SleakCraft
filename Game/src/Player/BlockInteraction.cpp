#include "Player/BlockInteraction.hpp"

#include "MainScene.hpp"
#include "World/BlockEffects.hpp"
#include "World/ChunkManager.hpp"
#include <Camera/Camera.hpp>
#include <Debug/DebugLineRenderer.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Input/KeyCodes.hpp>
#include <Math/Vector.hpp>
#include <Physics/Colliders.hpp>

using namespace Sleak;
using namespace Sleak::Math;

void BlockInteraction::OnMousePressed(
    const Events::Input::MouseButtonPressedEvent& e) {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;

    // Only interact with the world when the mouse is captured (FPC active)
    auto* fpc = cam->GetComponent<FirstPersonController>();
    if (!fpc || !fpc->IsEnabled()) return;

    auto pos = cam->GetPosition();
    auto dir = cam->GetDirection();
    auto hit = m_chunkManager.VoxelRaycast(pos, dir, 6.0f);
    if (!hit.hit) return;

    MouseCode button = e.GetMouseButton();
    if (button == MouseCode::ButtonLeft) {
        BlockType broken = hit.blockType;
        m_chunkManager.SetBlockAt(hit.blockX, hit.blockY, hit.blockZ,
                                  BlockType::Air);
        if (broken != BlockType::Air)
            m_blockEffects.SpawnBreakEffect(hit.blockX, hit.blockY, hit.blockZ,
                                            broken);
    } else if (button == MouseCode::ButtonRight) {
        // Prevent placing a block inside the player's bounding box
        float feetY = pos.GetY() - 1.62f;
        bool overlaps = (hit.placeX + 1 > pos.GetX() - 0.3f &&
                         hit.placeX < pos.GetX() + 0.3f) &&
                        (hit.placeY + 1 > feetY && hit.placeY < feetY + 1.8f) &&
                        (hit.placeZ + 1 > pos.GetZ() - 0.3f &&
                         hit.placeZ < pos.GetZ() + 0.3f);
        if (!overlaps)
            m_blockEffects.SpawnPlaceEffect(hit.placeX, hit.placeY, hit.placeZ,
                                            m_selectedBlock);
    }
}

void BlockInteraction::OnMouseScrolled(
    const Events::Input::MouseScrolledEvent& e) {
    float y = e.GetYOffset();
    if (y > 0.0f) {
        m_selectedSlot--;
        if (m_selectedSlot < 0) m_selectedSlot = HOTBAR_SLOTS - 1;
    } else if (y < 0.0f) {
        m_selectedSlot++;
        if (m_selectedSlot >= HOTBAR_SLOTS) m_selectedSlot = 0;
    }
    m_selectedBlock = m_hotbar[m_selectedSlot];
}

void BlockInteraction::OnKeyPressed(
    const Events::Input::KeyPressedEvent& e) {
    if_key_press(KEY__1) {
        m_selectedSlot = 0;
        m_selectedBlock = m_hotbar[0];
    }
    if_key_press(KEY__2) {
        m_selectedSlot = 1;
        m_selectedBlock = m_hotbar[1];
    }
    if_key_press(KEY__3) {
        m_selectedSlot = 2;
        m_selectedBlock = m_hotbar[2];
    }
    if_key_press(KEY__4) {
        m_selectedSlot = 3;
        m_selectedBlock = m_hotbar[3];
    }
    if_key_press(KEY__5) {
        m_selectedSlot = 4;
        m_selectedBlock = m_hotbar[4];
    }
    if_key_press(KEY__6) {
        m_selectedSlot = 5;
        m_selectedBlock = m_hotbar[5];
    }
    if_key_press(KEY__7) {
        m_selectedSlot = 6;
        m_selectedBlock = m_hotbar[6];
    }
    if_key_press(KEY__8) {
        m_selectedSlot = 7;
        m_selectedBlock = m_hotbar[7];
    }
    if_key_press(KEY__9) {
        m_selectedSlot = 8;
        m_selectedBlock = m_hotbar[8];
    }
}

void BlockInteraction::RenderOutline() {
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;

    // Block outline always visible
    auto dir = cam->GetDirection();
    auto rayHit = m_chunkManager.VoxelRaycast(cam->GetPosition(), dir, 6.0f);
    if (rayHit.hit) {
        constexpr float E = 0.002f;
        Physics::AABB blockAABB(
            Vector3D(rayHit.blockX - E, rayHit.blockY - E,
                     rayHit.blockZ - E),
            Vector3D(rayHit.blockX + 1.0f + E, rayHit.blockY + 1.0f + E,
                     rayHit.blockZ + 1.0f + E));
        DebugLineRenderer::DrawAABB(blockAABB, 0.0f, 0.0f, 0.0f);
    }
}

void BlockInteraction::DrainCompletedPlacements() {
    for (auto& completed : m_blockEffects.PopCompletedPlacements())
        m_chunkManager.SetBlockAt(completed.x, completed.y, completed.z,
                                  completed.type);
}
