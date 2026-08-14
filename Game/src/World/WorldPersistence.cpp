#include "World/WorldPersistence.hpp"
#include "MainScene.hpp"
#include "World/BlockEffects.hpp"
#include "World/ChunkManager.hpp"
#include "World/SaveManager.hpp"
#include "World/WorldMeta.hpp"
#include <Camera/Camera.hpp>
#include <Core/CommandLine.hpp>
#include <Core/Logger.hpp>
#include <ECS/Components/FirstPersonController.hpp>
#include <Physics/RigidbodyComponent.hpp>
#include <cstring>

using namespace Sleak;

void WorldPersistence::SaveGame() {
    if (m_scene.IsSaveLocked()) {
        m_scene.SetSaveMessage("Saving disabled: save on disk is corrupted!",
                                3.0f);
        return;
    }
    auto* cam = m_scene.GetActiveCamera();
    if (!cam) return;

    WorldMeta meta;
    meta.worldName = m_scene.GetWorldName();
    meta.seed = m_chunkManager.GetSeed();
    auto pos = cam->GetPosition();
    meta.player.posX = pos.GetX();
    meta.player.posY = pos.GetY();
    meta.player.posZ = pos.GetZ();

    auto* fpc = cam->GetComponent<FirstPersonController>();
    if (fpc) {
        meta.player.pitch = fpc->GetPitch();
        meta.player.yaw = fpc->GetYaw();
    }

    meta.player.selectedBlock =
        static_cast<uint8_t>(m_scene.GetSelectedBlock());
    meta.player.renderDistance = m_chunkManager.GetRenderDistance();

    // Commit placements still inside the 0.15s animation window
    for (auto& p : m_blockEffects.DrainAllPlacements())
        m_chunkManager.SetBlockAt(p.x, p.y, p.z, p.type);
    m_chunkManager.FlushPendingEdits();

    // Collect dirty chunks
    auto dirtyInfos = m_chunkManager.GetDirtyChunks();
    std::vector<ChunkSaveData> dirtyChunks;
    dirtyChunks.reserve(dirtyInfos.size());
    for (const auto& info : dirtyInfos) {
        ChunkSaveData cd;
        cd.cx = info.cx;
        cd.cy = info.cy;
        cd.cz = info.cz;
        std::memcpy(cd.blocks.data(), info.blockData, 4096);
        dirtyChunks.push_back(std::move(cd));
    }

    if (m_saveManager.SaveWorld(meta, dirtyChunks)) {
        m_chunkManager.ClearDirtyFlags();
        m_chunkManager.SaveHeightmapCache(m_saveManager.GetSavePath() +
                                           "/heightmap.cache");
        m_scene.SetSaveMessage("World Saved!", 2.0f);
    } else {
        m_scene.SetSaveMessage("Save Failed!", 3.0f);
    }
}

void WorldPersistence::LoadGame() {
    WorldMeta meta;
    std::unordered_map<int64_t, std::array<uint8_t, 4096>> chunkData;

    if (!m_saveManager.LoadWorld(meta, chunkData)) {
        if (m_saveManager.HasSave()) {
            // Unreadable save: never overwrite it with a fresh seed-0 world
            m_scene.SetSaveLocked(true);
            SLEAK_ERROR("Load: save exists but is unreadable — saving locked");
            m_scene.SetSaveMessage("Save corrupted! Saving disabled.", 6.0f);
        } else {
            m_scene.SetSaveMessage("No Save Found!", 2.0f);
        }
        return;
    }

    // Restore player state
    auto* cam = m_scene.GetActiveCamera();
    if (cam) {
        cam->SetPosition(
            {meta.player.posX, meta.player.posY, meta.player.posZ});
        auto* fpc = cam->GetComponent<FirstPersonController>();
        if (fpc) {
            fpc->SetPitch(meta.player.pitch);
            fpc->SetYaw(meta.player.yaw);
        }
        auto* rb = cam->GetComponent<RigidbodyComponent>();
        if (rb) rb->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    m_scene.SetSelectedBlock(static_cast<BlockType>(meta.player.selectedBlock));

    // ForceReload first: drains workers so reseed/data swap can't race Generate
    m_chunkManager.ForceReload();
    m_chunkManager.SetSeed(meta.seed);
    m_chunkManager.LoadChunkData(chunkData);
    m_chunkManager.LoadHeightmapCache(m_saveManager.GetSavePath() +
                                       "/heightmap.cache");

    // Load a small area synchronously, let the rest stream in
    m_chunkManager.SetRenderDistance(3);
    m_chunkManager.Update(meta.player.posX, meta.player.posY,
                           meta.player.posZ);
    m_chunkManager.FlushPendingChunks();
    {
        const std::string rdStr = Sleak::CommandLine::GetValue("-rd");
        int cliRD = rdStr.empty() ? 0 : std::stoi(rdStr);
        int rd =
            cliRD > 0 ? cliRD : static_cast<int>(meta.player.renderDistance);
        if (rd < 4) rd = 4;
        if (rd > 16) rd = 16;
        m_chunkManager.SetRenderDistance(rd);
    }
    m_chunkManager.SetMultithreaded(m_scene.IsMultithreadedLoading());

    m_scene.SetSaveMessage("World Loaded!", 2.0f);
}
