#pragma once
#include <cstdint>
#include <cstddef>
namespace Vulkan {
enum PersistentHistoryMode : uint32_t { PHClear=0, PHCapture=1, PHAdvance=2, PHAdopt=3, PHCompose=4 };
// std140, 8 complete 16-byte rows. Motion maps current UV to previous UV:
// previousUV = currentUV + motion * MvScale / motionExtent + jitterDelta.
struct alignas(16) PersistentHistoryConstants {
 uint32_t Mode{}, Width{}, Height{}, SourceState{};
 uint32_t DestState{}, DepthInverted{}, HistoryValid{}, FallbackState{};
 float MvScaleX{}, MvScaleY{}, JitterDeltaX{}, JitterDeltaY{};
 float Epsilon{1.f/512.f}, DepthTolerance{.02f}, ColourTolerance{.5f}, MaxLogEdit{4.f};
 float DeltaFrames{1.f}, DeltaMs{}, OriginAgeFrames{}, OriginAgeMs{};
 float MaxFrames{12.f}, MaxMs{200.f}, ExposureRatio{1.f}, MinConfidence{.5f};
 float CollectStats{}; // opt-in cold diagnostic counters; zero on the hot path
 float Reserved1[7]{};
};
static_assert(sizeof(PersistentHistoryConstants)==128);
// invalid[4], validPixels, rejectedPixels, changedPixels, oldestFrames.
struct PersistentHistoryState { uint32_t invalid[4], validPixels, rejectedPixels, changedPixels, oldestFrames; };
}
