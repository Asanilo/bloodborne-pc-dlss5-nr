// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace BbCp {
template <std::size_t Queues>
struct SubmissionWatermark {
    std::array<std::uint64_t, Queues> targets{};
    std::uint64_t frame{};
    bool IsDecoded(const std::array<std::uint64_t, Queues>& decoded) const noexcept {
        for (std::size_t q = 0; q < Queues; ++q)
            if (decoded[q] < targets[q]) return false;
        return true;
    }
};
}
