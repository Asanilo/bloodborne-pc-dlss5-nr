// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: hooks on the entry points of the eboot's libGnm (sce::Gnm command buffer methods, linked
// statically into the game: it writes its own PM4). They are the game's graphics API, the level a
// translation layer like DXVK sits at. BB_GNM_CENSUS=1 counts the calls per function and prints
// the busiest every 5 s; later steps replace the functions themselves.
#pragma once

#include <cstdint>

namespace BbGnmHooks {
/// Patches the image (the loader calls it once, before any game code runs, while the image is
/// still writable). Each entry is checked byte for byte against the 1.09 eboot.
void PatchImage(unsigned char* image, std::uint64_t size);

/// Cold-only, same-thread evidence. Addresses are host guest-image addresses; wrapper is an
/// image-relative offset. exact_record means the live loop index and callsite were verified,
/// never uniqueness inferred from a matching target. Unavailable fields remain zero.
struct ProducerContext {
    bool known = false;
    bool read_failed = false;
    bool exact_record = false;
    bool record_type_matches = false;
    bool record_target_matches = false;
    std::uint64_t wrapper = 0, caller = 0;
    std::uint64_t cb = 0, base = 0, end = 0, cursor = 0;
    std::uint64_t patcher_caller = 0, table = 0, records = 0, payload = 0, record_count = 0;
    std::uint64_t exact_record_index = 0, record_target = 0, record_payload = 0;
    std::uint32_t record[3]{};
    std::uint32_t payload_words[10]{};
    std::uint32_t target_before[40]{};
    std::uint32_t payload_word_count = 0, target_word_count = 0;
};
ProducerContext CurrentProducerContext(const std::uint32_t* commands);

/// BB_GNM_OBSERVE=1: checks a submitted command buffer against the packets the hooked API calls
/// wrote (the submitting thread; statistics in the census report).
void CheckSubmission(const std::uint32_t* commands, std::uint64_t dwords);

/// BB_GNM_OBSERVE: packets our GnmDriver writes into space the game reserved ([commands,
/// commands + dwords)), marked when the guard goes out of scope.
struct DriverWrite {
    std::uint32_t* commands;
    std::uint64_t dwords;
    ~DriverWrite();
};
} // namespace BbGnmHooks
