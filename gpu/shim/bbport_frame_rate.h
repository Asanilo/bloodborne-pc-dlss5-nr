// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game's frame rate set free by hooks in its code (BB_FPS=uncap, the default), not by
// patches: its frame pacer no longer waits for a 30 FPS frame, its step gets the frame's real time,
// and what it ties to a frame (a 30 FPS frame's time, a per-frame step, smoothing and damping) reads
// values the hooks recompute from that time every frame (bbport_frame_rate.cpp).
#pragma once

#include <cstdint>

namespace BbFrameRate {
/// Record-only proof for the two camera RIP-disp redirects. PatchImage clears it at entry;
/// entries are recorded only after the actual writer succeeds, and Getter returns a copy only
/// once both are complete. No Live values or game behavior are exposed.
struct CameraRedirectEntry {
    std::uint64_t site = 0, next = 0, constant = 0, target = 0;
    std::uint32_t original_disp = 0, written_disp = 0, field_offset = 0;
    std::uint8_t original[8] = {}, written[8] = {};
    bool applied = false;
};
struct CameraRedirectProof {
    std::uint64_t image = 0, size = 0, sequence = 0;
    CameraRedirectEntry entry[2];
    bool complete = false;
};
/// Copy of the last PatchImage's complete two-site proof; false if cleared/incomplete.
bool CameraRedirectProofs(CameraRedirectProof* out);
/// Last PatchImage call sequence (read-only; for binding a pre-writer snapshot to this call).
std::uint64_t CameraRedirectSequence();
/// Installs the hooks (at load, before the game runs: bbgpu_patch_image).
void PatchImage(unsigned char* image, std::uint64_t size);
/// Whether the hooks run the game's frame rate: the video out then shows a frame as soon as the
/// game submits it (the game's own flip rate asks for 30 FPS).
bool Unlocked();
/// A frame rate limit while the game runs (30..480; 0: the launched mode and limit). The game's
/// frame pacer waits for it itself. Lowers a running game to 30 FPS for what the unlocked rate
/// breaks (the cinematic of Laurence, the optional boss: a hang above 30 FPS, as with the
/// community 60 FPS patch).
void SetLimit(int fps);
int Limit();
} // namespace BbFrameRate
