// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbGxRenderOwner {
// Bloodborne 1.09: the global resource-copy facade and GXRenderThread both
// reference backend+0x130. Dispatch must return that CPU context only after
// the render job has finished constructing commands. Use the game's existing
// synchronous mode, including its event68 completion branch. Configure the
// factory rather than overriding a branch while reporting async mode elsewhere.
// Worker creation settings are untouched; GPU validation is still required.
// This is CPU command ownership, NOT proof of Vulkan completion/retirement.
constexpr std::size_t Dispatch = 0x21b6aa0;
constexpr std::size_t DispatchSize = 0x131;
constexpr std::uint64_t ExpectedDispatch = 0xb3d30b1c22c4e2d8ULL;
constexpr std::size_t Factory = 0x1d96b09;
constexpr std::size_t FactorySize = 32;
constexpr std::size_t ModeImmediate = 22; // config[0x72] = 1 in the original factory
constexpr std::uint64_t ExpectedFactory = 0x29bc1a96e03c3fdeULL;
enum class Result { Applied, AlreadyApplied, ImageMismatch };

inline Result Patch(unsigned char* image, std::size_t size) {
    if (!image || size < Dispatch || size - Dispatch < DispatchSize)
        return Result::ImageMismatch;
    const auto* code = image + Dispatch;
    auto* factory = image + Factory;
    if (factory[ModeImmediate] > 1) return Result::ImageMismatch;
    // Verify the dispatch and factory before changing the initial mode. No
    // private executable payload is embedded here. Never patch on a partial match.
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < DispatchSize; ++i) {
        hash ^= code[i];
        hash *= 1099511628211ULL;
    }
    if (hash != ExpectedDispatch) return Result::ImageMismatch;
    hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < FactorySize; ++i) {
        hash ^= i == ModeImmediate ? 1 : factory[i];
        hash *= 1099511628211ULL;
    }
    if (hash != ExpectedFactory) return Result::ImageMismatch;
    if (factory[ModeImmediate] == 0) return Result::AlreadyApplied;
    factory[ModeImmediate] = 0;
    return Result::Applied;
}
} // namespace BbGxRenderOwner
