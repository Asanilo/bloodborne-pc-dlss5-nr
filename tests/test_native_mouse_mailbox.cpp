// SPDX-License-Identifier: GPL-2.0-or-later
//
// CPU-only acceptance tests for BbNativeMouse::Mailbox stage 2.  No GPU, Vulkan, Wine, game
// image or save data is touched.  Times are injected explicitly for determinism.

#include "../gpu/shim/native_mouse_mailbox.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

using namespace BbNativeMouse;

namespace {

void fail(const char* file, int line, const char* expression) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    std::exit(1);
}

#define CHECK(expression) \
    do { \
        if (!(expression)) fail(__FILE__, __LINE__, #expression); \
    } while (false)

State snapshot(Mailbox& mailbox) {
    CHECK(TestSeam::TryAcquire(mailbox));
    State copy = mailbox.state;
    TestSeam::Release(mailbox);
    copy.gate = kGateFree;
    return copy;
}

Mailbox make_ready(std::uint64_t now_us = 1000) {
    Mailbox mailbox;
    mailbox.SetInstalled(true);
    const ConsumeResult heartbeat = mailbox.TryConsume(now_us);
    CHECK(heartbeat.status == ConsumeStatus::Empty);
    const State state = snapshot(mailbox);
    CHECK(state.ready == 1);
    CHECK(state.heartbeat_us == now_us);
    return mailbox;
}

void check_no_pending(Mailbox& mailbox) {
    const State state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.pitch == 0.0f);
    CHECK(state.yaw == 0.0f);
}

void test_layout_and_defaults() {
    static_assert(std::is_standard_layout_v<State>);
    static_assert(std::is_trivial_v<State>);
    static_assert(std::is_standard_layout_v<Mailbox>);
    static_assert(std::is_trivially_copyable_v<Mailbox>);
    static_assert(alignof(Mailbox) == 64);
    static_assert(sizeof(Mailbox) == 192);
    static_assert(offsetof(Mailbox, state) == 0);
    static_assert(offsetof(State, published_epoch) == 24);
    static_assert(offsetof(State, fatal) == 28);
    static_assert(offsetof(State, heartbeat_seq) == 96);
    static_assert(offsetof(State, reset_watermark) == 104);
    static_assert(offsetof(State, fallback_watermark) == 112);
    static_assert(offsetof(State, cancelled_seq) == 120);

    Mailbox mailbox;
    const State state = snapshot(mailbox);
    CHECK(state.gate == kGateFree);
    CHECK(state.installed == 0);
    CHECK(state.captured == 1);
    CHECK(state.ready == 0);
    CHECK(state.epoch == kInitialEpoch);
    CHECK(state.published_epoch == kInitialEpoch);
    CHECK(state.fatal == 0);
    CHECK(state.has_pending == 0);
    CHECK(state.sequence == 0);
    CHECK(state.heartbeat_seq == 0);
    CHECK(state.reset_watermark == 0);
    CHECK(state.fallback_watermark == 0);
    CHECK(state.cancelled_seq == 0);

    CHECK(!mailbox.Installed());
    CHECK(mailbox.Captured());
    CHECK(!mailbox.Enabled());
    CHECK(mailbox.Epoch() == kInitialEpoch);
    CHECK(mailbox.SnapshotEpoch() == kInitialEpoch);
}

void test_default_fallback_capture_and_epoch() {
    Mailbox mailbox;
    const std::uint64_t initial = mailbox.Epoch();
    CHECK(initial == kInitialEpoch);

    CHECK(mailbox.Submit(initial, 0.25f, 0.5f, 1000) == SubmitResult::Fallback);
    State state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.sequence == 0);
    CHECK(state.ready == 0);

    mailbox.SetCapture(false);
    CHECK(!mailbox.Captured());
    const std::uint64_t capture_off_epoch = mailbox.Epoch();
    CHECK(capture_off_epoch != initial);
    CHECK(mailbox.Submit(capture_off_epoch, 1.0f, 1.0f, 1001) == SubmitResult::Discard);
    mailbox.SetCapture(false);
    CHECK(mailbox.Epoch() == capture_off_epoch);

    mailbox.SetCapture(true);
    const std::uint64_t capture_on_epoch = mailbox.Epoch();
    CHECK(capture_on_epoch != capture_off_epoch);
    CHECK(mailbox.Submit(capture_off_epoch, 1.0f, 1.0f, 1001) == SubmitResult::Discard);
    CHECK(mailbox.Submit(capture_on_epoch, 1.0f, 1.0f, 1001) == SubmitResult::Fallback);
    CHECK(mailbox.Submit(capture_on_epoch + 1, 1.0f, 1.0f, 1001) == SubmitResult::Discard);

    Mailbox installed;
    const std::uint64_t before = installed.Epoch();
    installed.SetInstalled(false);
    CHECK(installed.Epoch() == before);
    installed.SetInstalled(true);
    const std::uint64_t after = installed.Epoch();
    CHECK(after != before);
    installed.SetInstalled(true);
    CHECK(installed.Epoch() == after);
    CHECK(installed.Installed());
    CHECK(installed.Enabled());
}

void test_first_native_needs_zero_heartbeat() {
    Mailbox mailbox;
    const std::uint64_t before = mailbox.Epoch();
    mailbox.SetInstalled(true);
    const std::uint64_t epoch = mailbox.Epoch();
    CHECK(epoch != before);
    State state = snapshot(mailbox);
    CHECK(state.reset_watermark == 0);

    CHECK(mailbox.Submit(epoch, 1.0f, 2.0f, 1000) == SubmitResult::Fallback);
    state = snapshot(mailbox);
    CHECK(state.ready == 0);
    CHECK(state.heartbeat_us == 0);
    CHECK(state.has_pending == 0);

    const ConsumeResult heartbeat = mailbox.TryConsume(1000);
    CHECK(heartbeat.status == ConsumeStatus::Empty);
    CHECK(!heartbeat.consumed());
    CHECK(heartbeat.pitch == 0.0f);
    CHECK(heartbeat.yaw == 0.0f);
    state = snapshot(mailbox);
    CHECK(state.ready == 1);
    CHECK(state.heartbeat_us == 1000);
    CHECK(state.heartbeat_seq == 1);
    CHECK(state.reset_watermark == 0);

    const std::uint64_t active_epoch = mailbox.Epoch();
    CHECK(active_epoch != epoch); // recovery heartbeat advances the generation
    CHECK(mailbox.Submit(active_epoch, 1.0f, 2.0f, 1000) == SubmitResult::Native);
    state = snapshot(mailbox);
    CHECK(state.has_pending == 1);
    CHECK(state.pending_first_seq == 1);
    CHECK(state.pending_last_seq == 1);
    CHECK(state.pending_since_us == 1000);
}

void test_consume_once_zero_motion_and_accumulate() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());

    CHECK(mailbox.Submit(epoch, 1.0f, 0.5f, 1000) == SubmitResult::Native);
    CHECK(mailbox.Submit(epoch, 0.25f, -0.25f, 1000) == SubmitResult::Native);
    State state = snapshot(mailbox);
    CHECK(state.pitch == 1.25f);
    CHECK(state.yaw == 0.25f);
    CHECK(state.pending_first_seq == 1);
    CHECK(state.pending_last_seq == 2);

    const ConsumeResult batch = mailbox.TryConsume(1000);
    CHECK(batch.status == ConsumeStatus::Batch);
    CHECK(batch.consumed());
    CHECK(batch.has_motion);
    CHECK(batch.pitch == 1.25f);
    CHECK(batch.yaw == 0.25f);
    CHECK(batch.epoch == epoch);
    CHECK(batch.first_sequence == 1);
    CHECK(batch.sequence == 2);
    state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.last_consumed_seq == 2);

    const ConsumeResult empty = mailbox.TryConsume(1000);
    CHECK(empty.status == ConsumeStatus::Empty);
    CHECK(!empty.consumed());
    CHECK(empty.pitch == 0.0f);
    CHECK(empty.yaw == 0.0f);

    CHECK(mailbox.Submit(epoch, -0.5f, 0.5f, 1000) == SubmitResult::Native);
    const ConsumeResult next = mailbox.TryConsume(1000);
    CHECK(next.status == ConsumeStatus::Batch);
    CHECK(next.sequence == 3);
    CHECK(next.sequence > batch.sequence);

    // Net-zero batches retire exactly once but carry no camera displacement.
    Mailbox zero = make_ready(2000);
    const std::uint32_t zero_epoch = static_cast<std::uint32_t>(zero.Epoch());
    CHECK(zero.Submit(zero_epoch, 1.0f, -2.0f, 2000) == SubmitResult::Native);
    CHECK(zero.Submit(zero_epoch, -1.0f, 2.0f, 2000) == SubmitResult::Native);
    const ConsumeResult retired = zero.TryConsume(2000);
    CHECK(retired.status == ConsumeStatus::Batch);
    CHECK(!retired.has_motion);
    CHECK(retired.pitch == 0.0f && retired.yaw == 0.0f);
    CHECK(retired.sequence == 2);
    CHECK(zero.TryConsume(2000).status == ConsumeStatus::Empty);
    const State zero_state = snapshot(zero);
    CHECK(zero_state.last_consumed_seq == 2);
    CHECK(zero_state.has_pending == 0);
}

void test_timeout_boundary_and_recovery() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.0f, 0.0f, 1000) == SubmitResult::Native);

    // 99,999 us is fresh; exactly 100,000 us is expired on both pending and heartbeat.
    CHECK(mailbox.Submit(epoch, 0.5f, 0.0f, 100999) == SubmitResult::Native);
    CHECK(mailbox.Submit(epoch, 9.0f, 9.0f, 101000) == SubmitResult::Discard);
    const std::uint32_t recovered_epoch =
        static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(recovered_epoch == epoch + 1);
    State state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.ready == 0);
    CHECK(state.heartbeat_us == 0);
    CHECK(state.fallback_watermark == state.heartbeat_seq);
    CHECK(state.cancelled_seq == 2);
    CHECK(state.pitch == 0.0f && state.yaw == 0.0f);

    CHECK(mailbox.Submit(epoch, 1.0f, 0.0f, 101000) == SubmitResult::Discard);
    CHECK(mailbox.Submit(recovered_epoch, 1.0f, 0.0f, 101000) == SubmitResult::Fallback);
    check_no_pending(mailbox);

    // The same invocation that detected expiry did not recover; this next zero heartbeat does.
    const ConsumeResult heartbeat = mailbox.TryConsume(101000);
    CHECK(heartbeat.status == ConsumeStatus::Empty);
    CHECK(!heartbeat.consumed());
    state = snapshot(mailbox);
    CHECK(state.heartbeat_seq == 2); // initial heartbeat then recovery heartbeat
    CHECK(state.ready == 1);
    CHECK(state.heartbeat_us == 101000);
    const std::uint64_t active_epoch = mailbox.Epoch();
    CHECK(active_epoch == static_cast<std::uint64_t>(recovered_epoch + 1));

    CHECK(mailbox.Submit(active_epoch, 2.0f, 0.0f, 101000) == SubmitResult::Native);
    const ConsumeResult batch = mailbox.TryConsume(101000);
    CHECK(batch.status == ConsumeStatus::Batch);
    CHECK(batch.pitch == 2.0f);
    CHECK(batch.sequence == 3);
}

void test_consume_timeout_boundary_and_recovery() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 2.0f, 3.0f, 1000) == SubmitResult::Native);

    ConsumeResult batch = mailbox.TryConsume(100999);
    CHECK(batch.status == ConsumeStatus::Batch);
    CHECK(batch.pitch == 2.0f);
    CHECK(batch.sequence == 1);

    CHECK(mailbox.Submit(epoch, 4.0f, 5.0f, 100999) == SubmitResult::Native);
    const ConsumeResult stale = mailbox.TryConsume(200999); // exactly 100 ms
    CHECK(stale.status == ConsumeStatus::Invalidated);
    CHECK(!stale.consumed());
    CHECK(stale.pitch == 0.0f && stale.yaw == 0.0f);
    const std::uint32_t recovered_epoch =
        static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(recovered_epoch == epoch + 1);
    State state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.ready == 0);
    CHECK(state.fallback_watermark == state.heartbeat_seq);
    CHECK(state.cancelled_seq == 2);

    const ConsumeResult empty = mailbox.TryConsume(200999);
    CHECK(empty.status == ConsumeStatus::Empty);
    CHECK(!empty.consumed());
    state = snapshot(mailbox);
    CHECK(state.ready == 1);
    CHECK(state.heartbeat_seq == 3); // initial, consumed batch, recovery heartbeat
    const std::uint64_t active_epoch = mailbox.Epoch();
    CHECK(active_epoch == static_cast<std::uint64_t>(recovered_epoch + 1));

    CHECK(mailbox.Submit(active_epoch, 8.0f, 9.0f, 200999) == SubmitResult::Native);
    batch = mailbox.TryConsume(200999);
    CHECK(batch.status == ConsumeStatus::Batch);
    CHECK(batch.pitch == 8.0f);
    CHECK(batch.yaw == 9.0f);
    CHECK(batch.sequence == 3);
}

void test_install_transitions_and_watermarks() {
    Mailbox mailbox;
    mailbox.SetInstalled(true);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.0f, 1.0f, 2000) == SubmitResult::Fallback);
    CHECK(mailbox.TryConsume(2000).status == ConsumeStatus::Empty);
    const std::uint32_t active1 = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(active1 != epoch); // recovery heartbeat advances the generation
    CHECK(mailbox.Submit(active1, 1.0f, 1.0f, 2000) == SubmitResult::Native);
    const State before_reset = snapshot(mailbox);
    CHECK(before_reset.heartbeat_seq == 1);

    mailbox.Cancel();
    const std::uint32_t off_epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(off_epoch != active1);
    State state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.reset_watermark == 1);
    CHECK(state.cancelled_seq == 1);
    CHECK(state.ready == 0);

    CHECK(mailbox.Submit(epoch, 1.0f, 1.0f, 2001) == SubmitResult::Discard);
    CHECK(mailbox.Submit(off_epoch, 1.0f, 1.0f, 2001) == SubmitResult::Fallback);
    CHECK(mailbox.TryConsume(2001).status == ConsumeStatus::Empty);
    state = snapshot(mailbox);
    CHECK(state.heartbeat_seq == 2);
    CHECK(state.ready == 1);
    const std::uint32_t active2 = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(active2 == off_epoch + 1);
    CHECK(mailbox.Submit(active2, 1.0f, 1.0f, 2001) == SubmitResult::Native);

    mailbox.SetInstalled(false);
    const std::uint32_t uninstalled_epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(uninstalled_epoch != active2);
    CHECK(mailbox.Submit(off_epoch, 1.0f, 1.0f, 2001) == SubmitResult::Discard);
    CHECK(mailbox.Submit(uninstalled_epoch, 1.0f, 1.0f, 2001) == SubmitResult::Fallback);
    CHECK(mailbox.TryConsume(2001).status == ConsumeStatus::Empty); // not installed: no heartbeat
    state = snapshot(mailbox);
    CHECK(state.heartbeat_seq == 2);

    mailbox.SetInstalled(true);
    const std::uint32_t reinstalled_epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(reinstalled_epoch != uninstalled_epoch);
    CHECK(mailbox.Submit(reinstalled_epoch, 1.0f, 1.0f, 2001) == SubmitResult::Fallback);
    CHECK(mailbox.TryConsume(2001).status == ConsumeStatus::Empty);
    const std::uint32_t active3 = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(active3 == reinstalled_epoch + 1);
    CHECK(mailbox.Submit(active3, 1.0f, 1.0f, 2001) == SubmitResult::Native);
}

void test_reset_cancels_only_unconsumed() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 0.1f, 0.2f, 1000) == SubmitResult::Native);
    const ConsumeResult consumed = mailbox.TryConsume(1000);
    CHECK(consumed.status == ConsumeStatus::Batch);
    CHECK(consumed.sequence == 1);

    CHECK(mailbox.Submit(epoch, 0.3f, 0.4f, 1000) == SubmitResult::Native);
    const std::uint32_t before_reset = static_cast<std::uint32_t>(mailbox.Epoch());
    mailbox.Reset();
    const std::uint32_t after_reset = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(after_reset != before_reset);
    State state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.ready == 0);
    CHECK(state.heartbeat_us == 0);
    CHECK(state.last_consumed_seq == 1);
    CHECK(state.cancelled_seq == 2);
    CHECK(state.sequence == 2);
    CHECK(state.reset_watermark == state.heartbeat_seq);

    CHECK(mailbox.Submit(before_reset, 0.5f, 0.5f, 1000) == SubmitResult::Discard);
    CHECK(mailbox.Submit(after_reset, 0.5f, 0.5f, 1000) == SubmitResult::Fallback);
    CHECK(mailbox.TryConsume(1000).status == ConsumeStatus::Empty);
    const std::uint32_t active = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(active == after_reset + 1);
    CHECK(mailbox.Submit(active, 0.5f, 0.5f, 1000) == SubmitResult::Native);
    const ConsumeResult next = mailbox.TryConsume(1000);
    CHECK(next.status == ConsumeStatus::Batch);
    CHECK(next.sequence == 3);
    CHECK(consumed.sequence == 1);
}

void test_invalid_inputs_and_cap() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.0f, 2.0f, 1000) == SubmitResult::Native);
    const State before = snapshot(mailbox);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(mailbox.Submit(epoch, nan, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(mailbox.Submit(epoch, 0.0f, inf, 1000) == SubmitResult::Discard);
    CHECK(mailbox.Submit(epoch, -inf, 0.0f, 1000) == SubmitResult::Discard);
    State after = snapshot(mailbox);
    CHECK(after.pitch == before.pitch && after.yaw == before.yaw);
    CHECK(after.pending_first_seq == before.pending_first_seq);
    CHECK(after.pending_last_seq == before.pending_last_seq);
    CHECK(after.pending_since_us == before.pending_since_us);
    CHECK(after.sequence == before.sequence);

    Mailbox capped = make_ready(1000);
    const std::uint32_t capped_epoch = static_cast<std::uint32_t>(capped.Epoch());
    CHECK(capped.Submit(capped_epoch, 15.5f, -15.5f, 1000) == SubmitResult::Native);
    CHECK(capped.Submit(capped_epoch, 0.5f, -0.5f, 1000) == SubmitResult::Native);
    State capped_state = snapshot(capped);
    CHECK(capped_state.pitch == 16.0f && capped_state.yaw == -16.0f);
    CHECK(capped.Submit(capped_epoch, 0.0001f, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(capped.Submit(capped_epoch, 0.0f, -0.0001f, 1000) == SubmitResult::Discard);
    CHECK(capped.Submit(capped_epoch, 16.0001f, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(capped.Submit(capped_epoch, std::numeric_limits<float>::max(), 0.0f, 1000) ==
          SubmitResult::Discard);
    after = snapshot(capped);
    CHECK(after.pitch == 16.0f && after.yaw == -16.0f);
    CHECK(after.pending_last_seq == capped_state.pending_last_seq);

    // Expiry cleanup runs before the native 16 rad bound: an expired pending batch is cleared
    // even when the invoking input is large.
    Mailbox stale = make_ready(1000);
    const std::uint32_t stale_epoch = static_cast<std::uint32_t>(stale.Epoch());
    CHECK(stale.Submit(stale_epoch, 15.9f, 0.0f, 1000) == SubmitResult::Native);
    const State stale_before = snapshot(stale);
    CHECK(stale.Submit(stale_epoch, 0.2f, 0.0f, 101000) == SubmitResult::Discard);
    const State stale_after = snapshot(stale);
    CHECK(stale_after.epoch == stale_before.epoch + 1);
    CHECK(stale_after.has_pending == 0);
    CHECK(stale_after.cancelled_seq == 1);
    CHECK(stale_after.pitch == 0.0f && stale_after.yaw == 0.0f);
    CHECK(stale.Submit(stale_epoch, 0.1f, 0.0f, 101000) == SubmitResult::Discard);
    const std::uint32_t recovered_epoch = static_cast<std::uint32_t>(stale.Epoch());
    CHECK(recovered_epoch == stale_epoch + 1);
    // Installed but not ready: finite large fallback is allowed after the timeout.
    CHECK(stale.Submit(recovered_epoch, 30.0f, 0.0f, 101000) == SubmitResult::Fallback);
    check_no_pending(stale);
}

void test_finite_limits_route_by_state() {
    const float large = 30.0f;
    const float max_finite = std::numeric_limits<float>::max();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // Default off: finite large movement stays on the legacy path.
    Mailbox disabled;
    const std::uint32_t off_epoch = static_cast<std::uint32_t>(disabled.Epoch());
    CHECK(disabled.Submit(off_epoch, large, 0.0f, 1000) == SubmitResult::Fallback);
    CHECK(disabled.Submit(off_epoch, max_finite, 0.0f, 1000) == SubmitResult::Fallback);
    CHECK(disabled.Submit(off_epoch, nan, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(disabled.Submit(off_epoch, 0.0f, inf, 1000) == SubmitResult::Discard);
    CHECK(disabled.Submit(off_epoch - 1, large, 0.0f, 1000) == SubmitResult::Discard);

    // Installed but no zero heartbeat: finite large movement is still fallback.
    Mailbox not_ready;
    not_ready.SetInstalled(true);
    const std::uint32_t not_ready_epoch = static_cast<std::uint32_t>(not_ready.Epoch());
    CHECK(not_ready.Submit(not_ready_epoch, large, 0.0f, 1000) == SubmitResult::Fallback);
    CHECK(not_ready.Submit(not_ready_epoch, max_finite, 0.0f, 1000) == SubmitResult::Fallback);
    CHECK(not_ready.Submit(not_ready_epoch, nan, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(not_ready.Submit(not_ready_epoch, 0.0f, -inf, 1000) == SubmitResult::Discard);
    CHECK(not_ready.Submit(not_ready_epoch - 1, large, 0.0f, 1000) == SubmitResult::Discard);

    // Native-ready: the 16 rad queue invariant rejects large finite input.
    Mailbox ready = make_ready(1000);
    const std::uint32_t ready_epoch = static_cast<std::uint32_t>(ready.Epoch());
    CHECK(ready.Submit(ready_epoch, large, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(ready.Submit(ready_epoch, max_finite, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(ready.Submit(ready_epoch, nan, 0.0f, 1000) == SubmitResult::Discard);
    CHECK(ready.Submit(ready_epoch, inf, 0.0f, 1000) == SubmitResult::Discard);
    check_no_pending(ready);

    // Over-limit Native input must not pollute an existing fresh batch.
    CHECK(ready.Submit(ready_epoch, 1.0f, 0.0f, 1000) == SubmitResult::Native);
    const State before = snapshot(ready);
    CHECK(ready.Submit(ready_epoch, large, 0.0f, 1000) == SubmitResult::Discard);
    const State after = snapshot(ready);
    CHECK(after.has_pending == before.has_pending);
    CHECK(after.pitch == before.pitch);
    CHECK(after.sequence == before.sequence);
}

void test_blocked_capability_and_clock() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.0f, 0.0f, 1000) == SubmitResult::Native);
    const State before = snapshot(mailbox);

    const ConsumeResult unmasked = mailbox.TryConsume(1000, false, true);
    CHECK(unmasked.status == ConsumeStatus::Blocked);
    CHECK(!unmasked.consumed());
    State after = snapshot(mailbox);
    CHECK(std::memcmp(&before, &after, sizeof(State)) == 0);

    const ConsumeResult no_clock = mailbox.TryConsume(1000, true, false);
    CHECK(no_clock.status == ConsumeStatus::Blocked);
    CHECK(!no_clock.consumed());
    after = snapshot(mailbox);
    CHECK(std::memcmp(&before, &after, sizeof(State)) == 0);

    const ConsumeResult batch = mailbox.TryConsume(1000, true, true);
    CHECK(batch.status == ConsumeStatus::Batch);
    CHECK(batch.has_motion);
    CHECK(batch.pitch == 1.0f);
}

void test_backwards_time_and_cancelled_seq() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.0f, 0.0f, 1000) == SubmitResult::Native);
    CHECK(mailbox.Submit(epoch, 2.0f, 0.0f, 1000) == SubmitResult::Native);
    const std::uint32_t before = static_cast<std::uint32_t>(mailbox.Epoch());

    CHECK(mailbox.Submit(epoch, 1.0f, 0.0f, 999) == SubmitResult::Discard);
    const std::uint32_t recovered_epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(recovered_epoch == before + 1);
    State state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.ready == 0);
    CHECK(state.heartbeat_us == 0);
    CHECK(state.cancelled_seq == 2);
    CHECK(state.reset_watermark == state.heartbeat_seq);

    CHECK(mailbox.Submit(epoch, 1.0f, 0.0f, 999) == SubmitResult::Discard);
    CHECK(mailbox.Submit(recovered_epoch, 1.0f, 0.0f, 999) == SubmitResult::Fallback);
    CHECK(mailbox.TryConsume(999).status == ConsumeStatus::Empty);
    const std::uint32_t active1 = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(active1 == recovered_epoch + 1); // recovery advances the generation
    CHECK(mailbox.Submit(active1, 1.0f, 0.0f, 999) == SubmitResult::Native);

    const ConsumeResult stale = mailbox.TryConsume(998);
    CHECK(stale.status == ConsumeStatus::Invalidated);
    CHECK(!stale.consumed());
    const std::uint32_t after_backwards_consume =
        static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(after_backwards_consume == active1 + 1);
    state = snapshot(mailbox);
    CHECK(state.has_pending == 0);
    CHECK(state.ready == 0);
    CHECK(state.cancelled_seq == 3);
    CHECK(state.reset_watermark == state.heartbeat_seq);

    CHECK(mailbox.TryConsume(998).status == ConsumeStatus::Empty);
    const std::uint32_t active2 = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(active2 == after_backwards_consume + 1);
    CHECK(mailbox.Submit(active2, 2.0f, 0.0f, 998) == SubmitResult::Native);
    const ConsumeResult batch = mailbox.TryConsume(998);
    CHECK(batch.status == ConsumeStatus::Batch);
    CHECK(batch.pitch == 2.0f);
    CHECK(batch.sequence == 4); // the cancelled recovered batch keeps its sequence
}

void test_fatal_overflow_disables_candidate() {
    Mailbox sequence_mailbox = make_ready(1000);
    const std::uint32_t sequence_epoch =
        static_cast<std::uint32_t>(sequence_mailbox.Epoch());
    CHECK(TestSeam::TryAcquire(sequence_mailbox));
    sequence_mailbox.state.sequence = UINT64_MAX;
    TestSeam::Release(sequence_mailbox);
    CHECK(sequence_mailbox.Submit(sequence_epoch, 1.0f, 0.0f, 1000) ==
          SubmitResult::Discard);
    CHECK(sequence_mailbox.Epoch() == 0);
    State sequence_state = snapshot(sequence_mailbox);
    CHECK(sequence_state.fatal == 1);
    CHECK(sequence_state.has_pending == 0);
    CHECK(!sequence_mailbox.Enabled());

    Mailbox epoch_mailbox;
    epoch_mailbox.SetInstalled(true);
    CHECK(TestSeam::TryAcquire(epoch_mailbox));
    epoch_mailbox.state.epoch = UINT32_MAX;
    __atomic_store_n(&epoch_mailbox.state.published_epoch, UINT32_MAX, __ATOMIC_RELEASE);
    TestSeam::Release(epoch_mailbox);
    epoch_mailbox.Cancel();
    CHECK(epoch_mailbox.Epoch() == 0);
    State epoch_state = snapshot(epoch_mailbox);
    CHECK(epoch_state.fatal == 1);
    CHECK(epoch_state.epoch == UINT32_MAX);
    CHECK(!epoch_mailbox.Enabled());

    Mailbox heartbeat_mailbox = make_ready(1000);
    CHECK(TestSeam::TryAcquire(heartbeat_mailbox));
    heartbeat_mailbox.state.heartbeat_seq = UINT64_MAX;
    TestSeam::Release(heartbeat_mailbox);
    const ConsumeResult heartbeat = heartbeat_mailbox.TryConsume(1000);
    CHECK(heartbeat.status == ConsumeStatus::Invalidated);
    CHECK(heartbeat_mailbox.Epoch() == 0);
    const State heartbeat_state = snapshot(heartbeat_mailbox);
    CHECK(heartbeat_state.fatal == 1);
    CHECK(!heartbeat_mailbox.Enabled());
}

void test_epoch_is_gate_free_and_published() {
    Mailbox mailbox;
    CHECK(mailbox.Epoch() == kInitialEpoch);
    CHECK(TestSeam::TryAcquire(mailbox));
    const std::uint32_t published = mailbox.Epoch();
    TestSeam::Release(mailbox);
    CHECK(published == kInitialEpoch);

    mailbox.Cancel();
    const std::uint32_t after_cancel = mailbox.Epoch();
    const State state = snapshot(mailbox);
    CHECK(after_cancel == state.epoch);
    CHECK(after_cancel == state.published_epoch);
}

void test_unproven_pending_fails_closed() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.0f, 2.0f, 1000) == SubmitResult::Native);
    CHECK(TestSeam::TryAcquire(mailbox));
    mailbox.state.ready = 0;
    TestSeam::Release(mailbox);
    const ConsumeResult result = mailbox.TryConsume(1000);
    CHECK(result.status == ConsumeStatus::Invalidated);
    CHECK(!result.consumed());
    check_no_pending(mailbox);
}

void test_gate_contention_is_one_try_and_side_effect_free() {
    Mailbox mailbox = make_ready(1000);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.5f, -2.5f, 1000) == SubmitResult::Native);

    CHECK(TestSeam::TryAcquire(mailbox));
    const State before = mailbox.state;
    const auto start = std::chrono::steady_clock::now();
    const ConsumeResult contended = mailbox.TryConsume(1000, true);
    const ConsumeResult contended_unmasked = mailbox.TryConsume(1000, false);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const State after = mailbox.state;
    CHECK(contended.status == ConsumeStatus::Contended);
    CHECK(contended_unmasked.status == ConsumeStatus::Blocked);
    CHECK(!contended.consumed());
    CHECK(contended.pitch == 0.0f && contended.yaw == 0.0f);
    CHECK(elapsed < std::chrono::milliseconds(100));
    CHECK(std::memcmp(&before, &after, sizeof(State)) == 0);
    TestSeam::Release(mailbox);

    const ConsumeResult batch = mailbox.TryConsume(1000);
    CHECK(batch.status == ConsumeStatus::Batch);
    CHECK(batch.pitch == 1.5f);
    CHECK(batch.yaw == -2.5f);
    CHECK(mailbox.TryConsume(1000).status == ConsumeStatus::Empty);
}

void test_concurrent_host_reset_submit_and_guest_consume() {
    constexpr int kSubmits = 4000;
    constexpr int kConsumes = kSubmits * 2;
    constexpr int kResets = 200;
    constexpr std::uint64_t kNow = 1'000'000;

    Mailbox mailbox;
    mailbox.SetInstalled(true);
    CHECK(mailbox.TryConsume(kNow).status == ConsumeStatus::Empty);

    std::atomic<bool> go{false};
    std::uint64_t submit_counts[3] = {0, 0, 0};
    std::uint64_t consume_counts[5] = {0, 0, 0, 0, 0};
    std::uint64_t consumed_batches = 0;
    std::uint64_t last_consumed_sequence = 0;
    std::atomic<bool> sequence_ok{true};

    std::thread producer([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < kSubmits; ++i) {
            const std::uint64_t epoch = mailbox.Epoch();
            const SubmitResult result = mailbox.Submit(
                static_cast<std::uint32_t>(epoch), 0.001f, -0.002f, kNow);
            ++submit_counts[static_cast<std::size_t>(result)];
        }
    });

    std::thread consumer([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < kConsumes; ++i) {
            const ConsumeResult result = mailbox.TryConsume(kNow);
            ++consume_counts[static_cast<std::size_t>(result.status)];
            if (result.status == ConsumeStatus::Batch) {
                if (!std::isfinite(result.pitch) || !std::isfinite(result.yaw))
                    sequence_ok.store(false, std::memory_order_relaxed);
                if (result.sequence <= last_consumed_sequence)
                    sequence_ok.store(false, std::memory_order_relaxed);
                last_consumed_sequence = result.sequence;
                ++consumed_batches;
            }
        }
    });

    std::thread resetter([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < kResets; ++i) {
            mailbox.Reset();
            if ((i % 5) == 0) {
                mailbox.SetCapture(false);
                mailbox.SetCapture(true);
            }
        }
    });

    go.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    resetter.join();

    CHECK(sequence_ok.load(std::memory_order_relaxed));
    CHECK(submit_counts[static_cast<std::size_t>(SubmitResult::Native)] +
              submit_counts[static_cast<std::size_t>(SubmitResult::Fallback)] +
              submit_counts[static_cast<std::size_t>(SubmitResult::Discard)] ==
          static_cast<std::uint64_t>(kSubmits));
    CHECK(consume_counts[static_cast<std::size_t>(ConsumeStatus::Empty)] +
              consume_counts[static_cast<std::size_t>(ConsumeStatus::Batch)] +
              consume_counts[static_cast<std::size_t>(ConsumeStatus::Contended)] +
              consume_counts[static_cast<std::size_t>(ConsumeStatus::Invalidated)] +
              consume_counts[static_cast<std::size_t>(ConsumeStatus::Blocked)] ==
          static_cast<std::uint64_t>(kConsumes));
    CHECK(consumed_batches <= submit_counts[static_cast<std::size_t>(SubmitResult::Native)]);

    mailbox.Reset();
    CHECK(mailbox.TryConsume(kNow).status == ConsumeStatus::Empty);
    const std::uint32_t epoch = static_cast<std::uint32_t>(mailbox.Epoch());
    CHECK(mailbox.Submit(epoch, 1.0f, 0.0f, kNow) == SubmitResult::Native);
    const ConsumeResult last = mailbox.TryConsume(kNow);
    CHECK(last.status == ConsumeStatus::Batch);
    CHECK(last.sequence > last_consumed_sequence);
    CHECK(mailbox.TryConsume(kNow).status == ConsumeStatus::Empty);

    std::printf(
        "concurrency: native=%llu fallback=%llu discard=%llu | empty=%llu batch=%llu "
        "contended=%llu invalidated=%llu blocked=%llu | consumed_batches=%llu\n",
        static_cast<unsigned long long>(
            submit_counts[static_cast<std::size_t>(SubmitResult::Native)]),
        static_cast<unsigned long long>(
            submit_counts[static_cast<std::size_t>(SubmitResult::Fallback)]),
        static_cast<unsigned long long>(
            submit_counts[static_cast<std::size_t>(SubmitResult::Discard)]),
        static_cast<unsigned long long>(
            consume_counts[static_cast<std::size_t>(ConsumeStatus::Empty)]),
        static_cast<unsigned long long>(
            consume_counts[static_cast<std::size_t>(ConsumeStatus::Batch)]),
        static_cast<unsigned long long>(
            consume_counts[static_cast<std::size_t>(ConsumeStatus::Contended)]),
        static_cast<unsigned long long>(
            consume_counts[static_cast<std::size_t>(ConsumeStatus::Invalidated)]),
        static_cast<unsigned long long>(
            consume_counts[static_cast<std::size_t>(ConsumeStatus::Blocked)]),
        static_cast<unsigned long long>(consumed_batches));
}

} // namespace

int main() {
    test_layout_and_defaults();
    test_default_fallback_capture_and_epoch();
    test_first_native_needs_zero_heartbeat();
    test_consume_once_zero_motion_and_accumulate();
    test_timeout_boundary_and_recovery();
    test_consume_timeout_boundary_and_recovery();
    test_install_transitions_and_watermarks();
    test_reset_cancels_only_unconsumed();
    test_invalid_inputs_and_cap();
    test_finite_limits_route_by_state();
    test_blocked_capability_and_clock();
    test_backwards_time_and_cancelled_seq();
    test_fatal_overflow_disables_candidate();
    test_epoch_is_gate_free_and_published();
    test_unproven_pending_fails_closed();
    test_gate_contention_is_one_try_and_side_effect_free();
    test_concurrent_host_reset_submit_and_guest_consume();
    std::puts("PASS: native mouse mailbox stage2 acceptance");
    return 0;
}
