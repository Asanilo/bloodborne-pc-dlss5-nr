// SPDX-License-Identifier: GPL-2.0-or-later
// bbport upscale: FSR 3.1 frame generation (the portable Vulkan interface of FSR-Vulkan, in
// third_party/fsr-vulkan). A frame interpolated between each two of the game's is shown halfway
// between them.
//
// Where its inputs come from (all in the draw stream, on the recording thread):
//  - CaptureScene, at the end of the upscaler's scaled run: the frame's depth is copied (the game
//    reuses its depth buffer before the display pass) and its motion vectors and camera kept.
//  - Interpolate, at the game's display pass: the output-size image holds the finished frame,
//    UI and all. Prepare (optical flow on that frame) and the interpolation are recorded there,
//    before the next frame's work. DLSSG waits for the guest display draw as well: its brightness
//    adjustment must be present in both real and generated images. No HUD-less image: the game
//    draws more than its HUD over the upscaled scene, and FSR pastes whatever differs
//    uninterpolated (a double image).
//  - The presenter encodes that output like the real frame (PrepareFrame) and shows it first.
// Settings: frame_generation in bbport.ini, the in-game menu, BB_FRAME_GEN=1.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "common/types.h"
#include "textures/image.h"
#include "vulkan/vk_common.h"

struct FfxVkPortableFrameGenerationContext;
struct FfxVkFsr3_3_1_6FrameGenerationContext;
namespace VideoCore { struct Buffer; }

namespace Vulkan {

struct DlssgOutputTicket;
class Instance;
class Scheduler;
class DlssgInputCapture;
class DlssgCpuBridge;
class DlssgGpuBridge;

class FrameGeneration {
public:
    FrameGeneration(const Instance& instance, Scheduler& scheduler);
    ~FrameGeneration();

    /// The setting is on and the GPU can.
    bool Wanted() const;

    /// Real game-frame boundary only. Missing scenes, toggles and long pauses reset FG history.
    void OnFrameStart();
    void Retire(); ///< Recording owner only, after exit has frozen new FG accesses.

    /// What the scene of this frame was rendered with (render size: depth and motion).
    struct Scene {
        vk::Image color;   ///< output-size General; native HDR route binds LDR later at UI
        vk::Format color_format;
        u32 width, height; ///< output size
        vk::Image depth;   ///< render size, General
        vk::Format depth_format;
        vk::Image motion;  ///< render size, RG16F pixels (previous - current), General
        u32 render_width, render_height;
        float jitter_x, jitter_y;
        float frame_ms;
        float camera_near, camera_far, vertical_fov;
        bool reset;
        u64 source_game_frame; ///< Real renderer source only; never advanced by Present.
        /// The camera in the world (FSR's frame interpolation uses them).
        float camera_position[3], camera_up[3], camera_right[3], camera_forward[3];
    };
    /// End of the upscaler's scaled run (recording thread): keeps the HUD-less copy and the inputs.
    void CaptureScene(const Scene& scene);
    /// Validated scaled FG source domain; never a GPU-completion or NR-history proof.
    bool CompatibleScaledNrSource(u64 source,u32 rw,u32 rh,u32 ow,u32 oh) const;
    /// Before Interpolate: accepted GPU guides and the UI actually sampled by this display draw.
    u64 AcceptedDisplaySource(vk::Image color,u32 width,u32 height,u64 source,bool sampled_ui) const;
    /// Native HDR SR captures guides before tonemapping; bind its finished LDR target at UI.
    bool BindSceneColor(vk::Image color, vk::Format format, u32 width, u32 height, u64 source);

    /// The first UI draw of the frame is about to run over `color` (the scene's image): what it
    /// holds now is the frame without its UI, post processing included (recording thread).
    void CaptureHudless(vk::Image color);

    /// The game's display pass of `display` (recording thread): `color` (the scene's image) now
    /// holds the frame with its UI. Records the interpolation if this frame's scene was captured.
    void Interpolate(vk::Image color, VAddr display);

    // DLSSG consumes the result of the guest display draw, including its brightness
    // adjustment, rather than assuming that draw is a byte-for-byte scene copy.
    bool DeferDisplayPass(VAddr display);
    bool HasDeferredDisplayPass(VAddr display) const;
    void FinishDisplayPass(VAddr display, vk::Image color, vk::Format format,
                           u32 width, u32 height);

    /// Presenter: the frame interpolated before the last real one of `display` (General layout,
    /// the scene's format and size), if there is one; it is used once.
    struct Output {
        vk::Image image;
        vk::ImageView view;
        vk::Format format;
        u32 width, height;
        u64 frame;
        std::shared_ptr<std::atomic<bool>> recorded;
        u64 source_game_frame;
        bool strict_source = false;
        u64 previous_source_game_frame = 0;
        std::shared_ptr<std::atomic<bool>> eligible; ///< Optional async model result, read after GPU readiness.
        u64 capture_cpu_ns = 0; ///< CPU guide-capture boundary; not input polling time.
    };
    bool TakeOutput(VAddr display, Output& out);
    std::shared_ptr<DlssgOutputTicket> TakeAsyncOutput(VAddr display);

private:
    bool EnsureContext(u32 render_width, u32 render_height, u32 width, u32 height, vk::Format format);
    void Destroy();
    void ResolveDeferredGpuOutput(VAddr display = 0);

    const Instance& instance;
    Scheduler& scheduler;
    bool supported = false;
    bool failed = false;

    FfxVkPortableFrameGenerationContext* context = nullptr;
    FfxVkFsr3_3_1_6FrameGenerationContext* context316 = nullptr;
    bool use316 = false;
    std::unique_ptr<DlssgInputCapture> dlssg_capture;
    std::unique_ptr<DlssgCpuBridge> dlssg_bridge;
    std::unique_ptr<DlssgGpuBridge> dlssg_gpu;
    std::shared_ptr<DlssgOutputTicket> async_output;
    vk::Image deferred_gpu_image;
    VAddr deferred_gpu_display = 0;
    std::shared_ptr<std::atomic<bool>> deferred_gpu_recorded;
    std::unique_ptr<VideoCore::Buffer> dlssg_upload;
    std::mutex context316_mutex; ///< Record/retire may run on different scheduler owners.
    u32 context_render_w = 0, context_render_h = 0, context_w = 0, context_h = 0;
    vk::Format context_format{};

    Scene scene{};
    VAddr pending_display_pass = 0;
    u64 accepted_scene_source = 0;
    u64 previous_fsr_source = 0;
    bool scene_captured = false; ///< this frame's, not yet interpolated
    u64 frame_id = 0;
    bool reset_next = true;
    bool dispatched_last_frame = false;
    u64 last_frame_ns = 0;
    u64 last_dispatch_ns = 0;
    u64 scene_id = 0;        ///< the captured scene's frame id (its Prepare's and Dispatch's)
    u64 scene_capture_ns = 0;
    bool scene_reset = true; ///< it starts over (nothing before it to interpolate from)
    /// The scene's depth, copied at the capture (the game reuses its depth buffer before the
    /// display pass, where Prepare runs).
    VideoCore::HostImage depth_copy;
    std::unique_ptr<VideoCore::Buffer> depth_transfer; ///< 3.1.6 consumes R32F, not D32S8.
    VideoCore::HostImage hudless;     ///< the frame without its UI (CaptureHudless)
    bool hudless_ready = false;       ///< taken for the captured scene
    vk::Format depth_copy_format{};
    u32 depth_copy_w = 0, depth_copy_h = 0;

    struct Target {
        VideoCore::HostImage image;
        vk::UniqueImageView view;
        vk::Format format{};
        u32 width = 0, height = 0;
        u64 frame = 0;
        u64 source_game_frame = 0;
        bool strict_source = false;
        u64 previous_source_game_frame = 0;
        VAddr display = 0;
        bool ready = false;
        std::shared_ptr<std::atomic<bool>> recorded;
        std::shared_ptr<std::atomic<bool>> eligible;
        u64 capture_cpu_ns = 0;
    };
    std::mutex targets_mutex;
    std::unordered_map<VAddr, Target> targets;
    Target& PrepareTarget(VAddr display, u64 id, bool reset);
};

} // namespace Vulkan
