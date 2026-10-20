// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: hooks on the entry points of the eboot's libGnm (sce::Gnm command buffer methods, linked
// statically into the game: it writes its own PM4). They are the game's graphics API, the level a
// translation layer like DXVK sits at. BB_GNM_CENSUS=1 counts the calls per function and prints
// the busiest every 5 s; later steps replace the functions themselves.
#pragma once

#include <cstdint>
#include <source_location>

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
    // Raw, bounded caller-frame observations only on cursor/command mismatch.
    std::uint64_t entry_rbp = 0;
    struct CallerFrame { std::uint64_t rbp = 0, next_rbp = 0, return_address = 0; };
    CallerFrame caller_frames[6]{};
    std::uint32_t caller_frame_count = 0;
    const char* caller_chain_stop = "not_requested";
};
ProducerContext CurrentProducerContext(const std::uint32_t* commands);

/// BB_GNM_OBSERVE=1: checks a submitted command buffer against the packets the hooked API calls
/// wrote (the submitting thread; statistics in the census report).
void CheckSubmission(const std::uint32_t* commands, std::uint64_t dwords);
/// BB_GNM_WRITES_TRACE: recent driver/native builders overlapping a bad submitted buffer.
/// BB_GNM_NATIVE_ONLY=1 installs native observers without the checksum/coverage analysis;
/// combine it with BB_GNM_WRITES_TRACE=1 to retain per-thread native write histories.
void ReportDriverWrites(std::uintptr_t begin, std::uint64_t bytes);

/// BB_GNM_OBSERVE: packets our GnmDriver writes into space the game reserved ([commands,
/// commands + dwords)), marked when the guard goes out of scope.
struct DriverWrite {
    std::uint32_t* commands;
    std::uint64_t dwords;
    const char* writer;
    DriverWrite(std::uint32_t* commands_, std::uint64_t dwords_,
                std::source_location where = std::source_location::current())
        : commands(commands_), dwords(dwords_), writer(where.function_name()) {}
    ~DriverWrite();
};
} // namespace BbGnmHooks
