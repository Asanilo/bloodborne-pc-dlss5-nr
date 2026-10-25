// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later
// Offline adaptation of BBSC's existing camera driver (bloodborne_re.cpp).
// Only the camera is installed: no network module, co-op patches or
// auto-spectate.
#include "bbport_cinematic.h"
#include "bbgpu.h"
#include "bbport_cinematic_input.h"
#include "bbport_guest_code_hooks.h"
#include "common/logging/log.h"
#include "common/types.h"
#include "games/game_profile.h"
#include <SDL3/SDL_events.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sys/uio.h>
#include <unistd.h>
extern "C" int runtime_memory_is_mapped(uintptr_t, uint64_t);
extern "C" void runtime_memory_note_write(uintptr_t, uint64_t);
namespace BbCinematic {
namespace {
alignas(16) std::array<u8, 0x200> storage{};
bool ReadablePointer(u64 address, size_t size) {
    if (!address || !size || size > 512 || address > std::numeric_limits<u64>::max() - size)
        return false;
    const u64 own = reinterpret_cast<u64>(storage.data());
    if (address >= own && address + size <= own + storage.size())
        return true;
    if (!runtime_memory_is_mapped(address, size))
        return false;
    std::array<u8, 512> bytes{};
    iovec local{bytes.data(), size}, remote{reinterpret_cast<void*>(address), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
}
bool ReadBytes(u64 address, void* bytes, size_t size) {
    if (!ReadablePointer(address, size))
        return false;
    iovec local{bytes, size}, remote{reinterpret_cast<void*>(address), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
}
template <class T> T ReadValue(u64 address, size_t offset) {
    T result{};
    ReadBytes(address + offset, &result, sizeof(T));
    return result;
}
bool WriteBytes(u64 address, const void* bytes, size_t size) {
    if (!ReadablePointer(address, size))
        return false;
    iovec local{const_cast<void*>(bytes), size}, remote{reinterpret_cast<void*>(address), size};
    if (process_vm_writev(getpid(), &local, 1, &remote, 1, 0) != ssize_t(size))
        return false;
    runtime_memory_note_write(address, size);
    return true;
}
template <class T> bool WriteValue(u64 address, size_t offset, T value) {
    return WriteBytes(address + offset, &value, sizeof(value));
}
s64 SteadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
struct BbscCineState {
    bool on = false;
    u64 director = 0;
    std::array<float, 3> pos{};
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f, fov = 0.0f, base_fov = 0.0f;
    std::array<float, 3> glide_pos{};
    float glide_yaw = 0.0f, glide_pitch = 0.0f, glide_roll = 0.0f, glide_fov = 0.0f;
    float speed = 4.0f;    // metres per second
    float glide_s = 0.35f; // seconds for the camera to catch up (0 = none)
    float right_sign = 1.0f, up_sign = 1.0f;
    s64 last_ms = 0;
    u64 fake_camera = 0; // our own camera object when the director has none
    u64 previous_camera = 0;
    s32 previous_enabled = 0;
};
BbscCineState bbsc_cine;
auto& bbsc_cine_camera_storage = storage;

void RestoreCamera() {
    if (!bbsc_cine.on)
        return;
    // Never overwrite a director which the game has already replaced.
    if (ReadablePointer(bbsc_cine.director, 0xD8)) {
        WriteValue<s32>(bbsc_cine.director, 0xC4, bbsc_cine.previous_enabled);
        if (bbsc_cine.fake_camera &&
            ReadValue<u64>(bbsc_cine.director, 0xD0) == bbsc_cine.fake_camera)
            WriteValue<u64>(bbsc_cine.director, 0xD0, bbsc_cine.previous_camera);
    }
    std::printf("BBPORT_CINEMATIC_OFFLINE_V1 restored: camera=(%.3f,%.3f,%.3f)\n",
                bbsc_cine.glide_pos[0], bbsc_cine.glide_pos[1], bbsc_cine.glide_pos[2]);
    bbsc_cine.on = false;
    bbsc_cine.fake_camera = 0;
}

void CameraFailed() {
    RestoreCamera();
    Core::BbPortCinematic::CinematicForceOff();
}

float BbscWrapAngle(float a) {
    constexpr float Pi = 3.14159265f;
    while (a > Pi)
        a -= 2.0f * Pi;
    while (a < -Pi)
        a += 2.0f * Pi;
    return a;
}

void BbscCineBasis(float yaw, float pitch, float roll, std::array<float, 3>& r,
                   std::array<float, 3>& u, std::array<float, 3>& f) {
    f = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    // right = up_world x forward (then the game's own handedness), up = forward x
    // right
    std::array<float, 3> r0{f[2], 0.0f, -f[0]};
    const float len = std::sqrt(r0[0] * r0[0] + r0[2] * r0[2]);
    r0 = len > 1e-5f ? std::array<float, 3>{r0[0] / len, 0.0f, r0[2] / len}
                     : std::array<float, 3>{1.0f, 0.0f, 0.0f};
    std::array<float, 3> u0{f[1] * r0[2] - f[2] * r0[1], f[2] * r0[0] - f[0] * r0[2],
                            f[0] * r0[1] - f[1] * r0[0]};
    const float c = std::cos(roll), sn = std::sin(roll);
    for (int i = 0; i < 3; ++i) {
        r[i] = (r0[i] * c + u0[i] * sn) * bbsc_cine.right_sign;
        u[i] = (u0[i] * c - r0[i] * sn) * bbsc_cine.up_sign;
    }
}

void __attribute__((sysv_abi)) CameraHook(u64, const Core::GuestRegisterSnapshot* registers) {
    if (!registers)
        return;
    const u64 director = registers->rdi;
    auto in = Core::BbPortCinematic::TakeCinematicInput();
    if (!in.enabled) {
        RestoreCamera();
        return;
    }
    if (!ReadablePointer(director, 0xD8)) {
        CameraFailed();
        return;
    }
    const u64 render = ReadValue<u64>(director, 0x10);
    if (!ReadablePointer(render, 0x60)) {
        CameraFailed();
        return;
    }
    const s64 now = SteadyNowMs();
    if (!bbsc_cine.on || bbsc_cine.director != director) {
        RestoreCamera();
        // Start where the game camera is.
        bbsc_cine.director = director;
        u64 debug_camera = ReadValue<u64>(director, 0xD0);
        bbsc_cine.previous_camera = debug_camera;
        bbsc_cine.previous_enabled = ReadValue<s32>(director, 0xC4);
        if (!ReadablePointer(debug_camera, 0x60)) {
            // No debug camera object in this build: give the director one of ours (a
            // plain matrix block is all 0x015715F0 reads from it when +0xC4 is set).
            if (!ReadBytes(render, bbsc_cine_camera_storage.data(), 0x60)) {
                CameraFailed();
                return;
            }
            debug_camera = reinterpret_cast<u64>(bbsc_cine_camera_storage.data());
            bbsc_cine.fake_camera = debug_camera;
            if (!WriteValue<u64>(director, 0xD0, debug_camera)) {
                CameraFailed();
                return;
            }
        }
        std::array<float, 16> m{};
        if (!ReadBytes(render + 0x10, m.data(), sizeof(m))) {
            WriteValue<u64>(director, 0xD0, bbsc_cine.previous_camera);
            CameraFailed();
            return;
        }
        const std::array<float, 3> f{m[8], m[9], m[10]};
        bbsc_cine.pos = {m[12], m[13], m[14]};
        bbsc_cine.pitch = std::asin(std::clamp(f[1], -1.0f, 1.0f));
        bbsc_cine.yaw = std::atan2(f[0], f[2]);
        bbsc_cine.roll = 0.0f;
        bbsc_cine.base_fov = ReadValue<float>(render, 0x50);
        bbsc_cine.fov = bbsc_cine.base_fov;
        bbsc_cine.right_sign = 1.0f;
        bbsc_cine.up_sign = 1.0f;
        std::array<float, 3> r{}, u{}, ff{};
        BbscCineBasis(bbsc_cine.yaw, bbsc_cine.pitch, 0.0f, r, u, ff);
        bbsc_cine.right_sign = r[0] * m[0] + r[1] * m[1] + r[2] * m[2] < 0.0f ? -1.0f : 1.0f;
        bbsc_cine.up_sign = u[0] * m[4] + u[1] * m[5] + u[2] * m[6] < 0.0f ? -1.0f : 1.0f;
        bbsc_cine.glide_pos = bbsc_cine.pos;
        bbsc_cine.glide_yaw = bbsc_cine.yaw;
        bbsc_cine.glide_pitch = bbsc_cine.pitch;
        bbsc_cine.glide_roll = 0.0f;
        bbsc_cine.glide_fov = bbsc_cine.fov;
        bbsc_cine.last_ms = now;
        bbsc_cine.on = true;
        std::printf("BBPORT_CINEMATIC_OFFLINE_V1 active: camera=(%.3f,%.3f,%.3f)\n",
                    bbsc_cine.pos[0], bbsc_cine.pos[1], bbsc_cine.pos[2]);
        LOG_INFO(Debug,
                 "BBSC cinematic camera: on at ({:.2f}, {:.2f}, {:.2f}) yaw {:.1f} "
                 "pitch {:.1f} fov "
                 "{:.3f} (director {:#x}, {} debug camera, signs {} {})",
                 bbsc_cine.pos[0], bbsc_cine.pos[1], bbsc_cine.pos[2], bbsc_cine.yaw * 57.2958f,
                 bbsc_cine.pitch * 57.2958f, bbsc_cine.fov, director,
                 bbsc_cine.fake_camera == debug_camera ? "BBSC's own" : "the game's",
                 bbsc_cine.right_sign, bbsc_cine.up_sign);
    }
    const float dt = std::clamp(static_cast<float>(now - bbsc_cine.last_ms) / 1000.0f, 0.0f, 0.1f);
    bbsc_cine.last_ms = now;

    // Look: mouse (right button held) and arrow keys.
    constexpr float MouseRadPerPixel = 0.0022f;
    constexpr float KeyRadPerSecond = 1.1f;
    const float zoom_scale = bbsc_cine.base_fov > 0.0f ? bbsc_cine.fov / bbsc_cine.base_fov : 1.0f;
    bbsc_cine.yaw += in.mouse_dx * MouseRadPerPixel * zoom_scale * bbsc_cine.right_sign;
    bbsc_cine.pitch -= in.mouse_dy * MouseRadPerPixel * zoom_scale;
    const float key_turn = KeyRadPerSecond * dt * zoom_scale * (in.slow ? 0.3f : 1.0f);
    bbsc_cine.yaw += ((in.look_right ? key_turn : 0.0f) - (in.look_left ? key_turn : 0.0f)) *
                     bbsc_cine.right_sign;
    bbsc_cine.pitch += (in.look_up ? key_turn : 0.0f) - (in.look_down ? key_turn : 0.0f);
    // Controller right stick: same rate as the arrow keys at full tilt.
    const float pad_turn = KeyRadPerSecond * dt * zoom_scale * (in.slow ? 0.3f : 1.0f);
    bbsc_cine.yaw += in.pad_look_x * pad_turn * bbsc_cine.right_sign;
    bbsc_cine.pitch += in.pad_look_y * pad_turn;
    bbsc_cine.pitch = std::clamp(bbsc_cine.pitch, -1.53f, 1.53f);
    // Roll (Dutch angle) and zoom.
    bbsc_cine.roll += ((in.roll_right ? 0.6f : 0.0f) - (in.roll_left ? 0.6f : 0.0f)) * dt;
    if (in.roll_reset) {
        bbsc_cine.roll = 0.0f;
    }
    const float zoom_rate = std::pow(0.55f, dt);
    if (in.zoom_in) {
        bbsc_cine.fov *= zoom_rate;
    }
    if (in.zoom_out) {
        bbsc_cine.fov /= zoom_rate;
    }
    if (in.pad_zoom != 0.0f) {
        bbsc_cine.fov *= std::pow(zoom_rate, in.pad_zoom); // positive = R2 = zoom in
    }
    if (in.zoom_reset) {
        bbsc_cine.fov = bbsc_cine.base_fov;
    }
    bbsc_cine.fov = std::clamp(bbsc_cine.fov, 0.08f, 2.2f);
    // Speed and glide.
    for (int i = 0; i < std::abs(in.wheel); ++i) {
        bbsc_cine.speed *= in.wheel > 0 ? 1.25f : 0.8f;
    }
    bbsc_cine.speed = std::clamp(bbsc_cine.speed, 0.2f, 80.0f);
    bbsc_cine.glide_s =
        std::clamp(bbsc_cine.glide_s + 0.1f * static_cast<float>(in.smooth_steps), 0.0f, 2.0f);
    if (in.wheel != 0 || in.smooth_steps != 0) {
        LOG_INFO(Debug, "BBSC cinematic camera: speed {:.1f} m/s, glide {:.1f} s", bbsc_cine.speed,
                 bbsc_cine.glide_s);
    }
    // Move in the camera's frame; up/down along the world's vertical.
    std::array<float, 3> r{}, u{}, f{};
    BbscCineBasis(bbsc_cine.yaw, bbsc_cine.pitch, 0.0f, r, u, f);
    const float step = bbsc_cine.speed * dt * (in.fast ? 4.0f : 1.0f) * (in.slow ? 0.25f : 1.0f);
    const float mf = std::clamp(
        (in.forward ? 1.0f : 0.0f) - (in.back ? 1.0f : 0.0f) + in.pad_move_y, -1.0f, 1.0f);
    const float mr =
        std::clamp((in.right ? 1.0f : 0.0f) - (in.left ? 1.0f : 0.0f) + in.pad_move_x, -1.0f, 1.0f);
    const float mu =
        std::clamp((in.up ? 1.0f : 0.0f) - (in.down ? 1.0f : 0.0f) + in.pad_rise, -1.0f, 1.0f);
    const std::array<float, 3>& side = r; // the game's right row is screen right
    for (int i = 0; i < 3; ++i) {
        bbsc_cine.pos[i] += (f[i] * mf + side[i] * mr) * step;
    }
    bbsc_cine.pos[1] += mu * step;

    // The glide: the drawn camera eases toward the flown one (a crane / steadicam
    // feel).
    const float k = bbsc_cine.glide_s > 0.0f ? 1.0f - std::exp(-dt / bbsc_cine.glide_s) : 1.0f;
    for (int i = 0; i < 3; ++i) {
        bbsc_cine.glide_pos[i] += (bbsc_cine.pos[i] - bbsc_cine.glide_pos[i]) * k;
    }
    bbsc_cine.glide_yaw += BbscWrapAngle(bbsc_cine.yaw - bbsc_cine.glide_yaw) * k;
    bbsc_cine.glide_pitch += (bbsc_cine.pitch - bbsc_cine.glide_pitch) * k;
    bbsc_cine.glide_roll += (bbsc_cine.roll - bbsc_cine.glide_roll) * k;
    bbsc_cine.glide_fov += (bbsc_cine.fov - bbsc_cine.glide_fov) * k;

    const u64 debug_camera = ReadValue<u64>(director, 0xD0);
    if (!ReadablePointer(debug_camera, 0x60) || !ReadablePointer(debug_camera + 0x10, 64)) {
        CameraFailed();
        return;
    }
    BbscCineBasis(bbsc_cine.glide_yaw, bbsc_cine.glide_pitch, bbsc_cine.glide_roll, r, u, f);
    alignas(16) std::array<float, 16> m{r[0],
                                        r[1],
                                        r[2],
                                        0.0f,
                                        u[0],
                                        u[1],
                                        u[2],
                                        0.0f,
                                        f[0],
                                        f[1],
                                        f[2],
                                        0.0f,
                                        bbsc_cine.glide_pos[0],
                                        bbsc_cine.glide_pos[1],
                                        bbsc_cine.glide_pos[2],
                                        1.0f};
    if (!WriteBytes(debug_camera + 0x10, m.data(), sizeof(m))) {
        CameraFailed();
        return;
    }
    // Projection: the game's current values (aspect, near, far), our field of
    // view.
    bool written = WriteValue<float>(debug_camera, 0x50, bbsc_cine.glide_fov);
    for (const size_t off : {0x54, 0x58, 0x5C}) {
        written = WriteValue<u32>(debug_camera, off, ReadValue<u32>(render, off)) && written;
    }
    if (!written || !WriteValue<s32>(director, 0xC4, 1))
        CameraFailed();
}

} // namespace
void Install() {
    const char* enabled = std::getenv("BB_CINEMATIC_CAMERA");
    if (!enabled || std::strcmp(enabled, "1") != 0)
        return;
    const char* online = std::getenv("BB_ONLINE");
    if (online && std::strcmp(online, "1") == 0) {
        std::puts("Offline F6 camera: online session keeps its existing module camera");
        return;
    }
    const auto* profile = Game::Active();
    if (!profile || !profile->name || std::strcmp(profile->name, "Bloodborne") != 0)
        return;
    constexpr u64 base = 0x800000000ull, offset = 0x015715f0;
    constexpr std::array<u8, 6> prologue{0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    if (!Core::InstallGuestCodeHook(reinterpret_cast<void*>(base + offset), prologue, 0,
                                    CameraHook)) {
        std::puts("BBPORT_CINEMATIC_OFFLINE_V1 unavailable: camera entry byte "
                  "check failed");
        return;
    }
    bbgpu_add_event_filter([](const void* event, void* window) -> int {
        return Core::BbPortCinematic::CinematicHandleEvent(*static_cast<const SDL_Event*>(event),
                                                           static_cast<SDL_Window*>(window));
    });
    std::puts("BBPORT_CINEMATIC_OFFLINE_V1 ready: F6, RMB look, WASD, E/Q rise, "
              "no network module");
}
} // namespace BbCinematic
