#pragma once
#include <cstdint>
#include <cstddef>
namespace Vulkan {
// Logical admission floor only. It never changes an origin identity or GPU lease.
struct PersistentHistoryResetWindow {
 uint64_t minimum_origin=0,generation=0;
 void ResetAt(uint64_t source) noexcept {if(source>minimum_origin)minimum_origin=source;++generation;}
 bool Admits(uint64_t immutable_origin)const noexcept{return immutable_origin>=minimum_origin;}
};
enum PersistentHistoryMode : uint32_t { PHClear=0, PHCapture=1, PHAdvance=2, PHAdopt=3, PHCompose=4, PHStats=5 };
// std140, 11 complete 16-byte rows. Motion maps current UV to previous UV:
// previousUV = currentUV + motion * MvScale / motionExtent + jitterDelta.
struct alignas(16) PersistentHistoryConstants {
 uint32_t Mode{}, Width{}, Height{}, SourceState{};
 uint32_t DestState{}, DepthInverted{}, HistoryValid{}, FallbackState{};
 float MvScaleX{}, MvScaleY{}, JitterDeltaX{}, JitterDeltaY{};
 float Epsilon{1.f/512.f}, DepthTolerance{.02f}, ColourTolerance{.5f}, MaxLogEdit{4.f};
 float DeltaFrames{1.f}, DeltaMs{}, OriginAgeFrames{}, OriginAgeMs{};
 // Capture confidence=1; advance min-combines evidence in [.5,1].
 // Default MinConfidence=.5 preserves existing hard depth/colour admission.
 float MaxFrames{12.f}, MaxMs{200.f}, ExposureRatio{1.f}, MinConfidence{.5f};
 float CollectStats{}; // opt-in cold diagnostic counters; zero on the hot path
 float CurrentJitterX{}, CurrentJitterY{}; // current raster viewport shift, pixels
 float DisplayStrength{1.f}; // compose-only; never stored into the history banks
 float Reserved1[4]{};
 float CurrentProjection[4]{}; // CameraMotion::TaaDepthParameters()[0]
 float PreviousProjection[4]{}; // [1]
 float PreviousViewZ[4]{}; // [2], previous view * current inverse view Z row
};
static_assert(sizeof(PersistentHistoryConstants)==176);
static_assert(offsetof(PersistentHistoryConstants, CurrentJitterX)==100);
static_assert(offsetof(PersistentHistoryConstants, DisplayStrength)==108);
static_assert(offsetof(PersistentHistoryConstants, CurrentProjection)==128);
static_assert(offsetof(PersistentHistoryConstants, PreviousProjection)==144);
static_assert(offsetof(PersistentHistoryConstants, PreviousViewZ)==160);
enum PersistentHistoryInvalid : uint32_t {
 PHBadRaw=1u, PHNonfiniteNr=2u, PHNegativeNr=4u, PHBadDepth=8u,
 PHExcessiveLogEdit=16u, PHBadModelAge=32u, PHGuideOutOfRange=64u,
 PHNonfiniteLogEdit=128u
};
// Reason bits accumulate over candidate pixels and survive every catch-up bank.
// PHExcessiveLogEdit marks pixel-local rejection (confidence=0), not bank failure.
// Every other bit remains bank-fatal. A mixed local/fatal status is fatal.
inline constexpr bool PersistentHistoryBankUsable(uint32_t status) {
 return (status & ~uint32_t(PHExcessiveLogEdit)) == 0u;
}
// invalid[4], validPixels, rejectedPixels, changedPixels, oldestFrames.
struct PersistentHistoryState { uint32_t invalid[4], validPixels, rejectedPixels, changedPixels, oldestFrames; };
}
