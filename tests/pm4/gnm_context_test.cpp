// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone CPU test: c++ -std=c++17 -O2 -pthread tests/pm4/gnm_context_test.cpp -o /tmp/gnm-context-test
#include "../../gpu/shim/bbport_gnm_hooks.cpp"
#include <cassert>

using namespace BbGnmHooks;
namespace {
u64 cb_words[3];
ProducerContext captured;
u64 observed_r12;
struct Pair { u64 a, b; };
using IntFn = Pair (*)(u64*, u64, u64, u64, u64, u64);
IntFn nested_fn;
Pair IntegerBody(u64* cb, u64 b, u64 c, u64 d, u64 e, u64 f) {
    assert(cb == cb_words && b == 2 && c == 3 && d == 4 && e == 5 && f == 6);
    observed_r12 = frames[depth - 1].entry_r12;
    captured = CurrentProducerContext(reinterpret_cast<const std::uint32_t*>(cb[2]));
    assert(captured.known && captured.wrapper == 0x1075840);
    assert(captured.target_word_count == 40);
    const auto mismatch = CurrentProducerContext(reinterpret_cast<const std::uint32_t*>(cb[2] + 4));
    assert(!mismatch.known && mismatch.cb == reinterpret_cast<u64>(cb) && mismatch.cursor == cb[2]);
    return {0x123456789abcdef0ull, 0xfedcba9876543210ull};
}
Pair NestedBody(u64* cb, u64 b, u64 c, u64 d, u64 e, u64 f) {
    const int before = depth;
    Pair result = nested_fn(cb, b, c, d, e, f);
    assert(depth == before && Table[frames[depth - 1].index].offset == 0x10753c0);
    return result;
}
double FloatingBody(u64* cb, u64 b, u64 c, u64 d, u64 e, u64 f,
                    double x0, double x1, double x2, double x3,
                    double x4, double x5, double x6, double x7) {
    assert(cb == cb_words && b == 2 && c == 3 && d == 4 && e == 5 && f == 6);
    assert(x0 == 1 && x1 == 2 && x2 == 3 && x3 == 4 && x4 == 5 && x5 == 6 && x6 == 7 && x7 == 8);
    assert(CurrentProducerContext(reinterpret_cast<const std::uint32_t*>(cb[2])).known);
    return x0+x1+x2+x3+x4+x5+x6+x7;
}
std::size_t Index(u64 offset) {
    for (std::size_t i = 0; i < Count; ++i) if (Table[i].offset == offset) return i;
    std::abort();
}
void Tail(u8* image, u64 offset, void* body) {
    const auto& entry = Table[Index(offset)];
    std::memcpy(image + offset, entry.prologue, entry.prologue_size);
    Writer w{image + offset + entry.prologue_size};
    // Undo precisely the verified moved prologue, then tail-call the C++ toy body. The real
    // entry stub and real exit thunk still handle arguments, return values and nested frames.
    w.Put({0x41, entry.prologue[5] == 0x56 ? 0x5e : 0x5f, 0x5d, 0x48, 0xb8});
    w.Imm64(reinterpret_cast<u64>(body)); w.Put({0xff, 0xe0});
}
void Run() {
    unsetenv("BB_GNM_CENSUS"); unsetenv("BB_GNM_OBSERVE"); unsetenv("BB_GNM_TRACE");
    constexpr std::size_t image_size = 0x1200000;
    auto* image = static_cast<u8*>(mmap(nullptr, image_size, PROT_READ|PROT_WRITE,
                                        MAP_PRIVATE|MAP_ANONYMOUS, -1, 0));
    assert(image != MAP_FAILED);
    Tail(image, 0x1075840, reinterpret_cast<void*>(&IntegerBody));
    Tail(image, 0x10753c0, reinterpret_cast<void*>(&NestedBody));
    Tail(image, 0x1075480, reinterpret_cast<void*>(&FloatingBody));
    // Executable parent patcher toy: preserve r12, set the actual loop index and reach the
    // real CS return offset. Both parent and child use the actual generated entry/exit stubs.
    const auto& patch = Table[Index(0x11fde00)];
    std::memcpy(image + patch.offset, patch.prologue, patch.prologue_size);
    Writer parent{image + patch.offset + patch.prologue_size};
    parent.Put({0x41,0x5f,0x5d,0x41,0x54,0x49,0xbc}); // undo prologue; push r12; mov r12,2
    parent.Imm64(2);
    parent.Put({0x48,0x89,0xf7}); // mov rdi,rsi (CB)
    parent.Put({0xbe}); parent.Imm32(2);
    parent.Put({0xba}); parent.Imm32(3);
    parent.Put({0xb9}); parent.Imm32(4);
    parent.Put({0x41,0xb8}); parent.Imm32(5);
    parent.Put({0x41,0xb9}); parent.Imm32(6);
    parent.Put({0xe9});
    std::int32_t branch = 0;
    assert(Rel32(parent.p + 4, image + 0x11fe65a, branch)); parent.Imm32(branch);
    Writer call{image + 0x11fe65a}; call.Put({0xe8});
    assert(Rel32(call.p + 4, image + 0x1075840, branch)); call.Imm32(branch);
    call.Put({0x41,0x5c,0xc3}); // pop r12; ret (through parent exit thunk)
    // Other two target entries deliberately mismatch; they must remain untouched.
    setenv("BB_NR_PM4_COLD_TRACE", "1", 1);
    unsetenv("BB_NR_PM4_PRODUCER_CONTEXT");
    PatchImage(image, image_size);
    assert(image[0x1075840] == 0x55 && !producer_context);
    setenv("BB_NR_PM4_PRODUCER_CONTEXT", "1", 1);
    setenv("BB_NR_PM4_COLD_TRACE", "0", 1);
    PatchImage(image, image_size);
    assert(image[0x1075840] == 0x55 && !producer_context);
    setenv("BB_NR_PM4_COLD_TRACE", "1", 1);
    setenv("BB_GNM_CENSUS_RANGE", "0:1", 1); // cold targets cannot be excluded by legacy census range
    PatchImage(image, image_size);
    assert(image[0x1075840] == 0xe9 && image[0x1075420] == 0);
    assert(!observe && producer_context);
    assert(mprotect(image, image_size, PROT_READ|PROT_EXEC) == 0);
    std::uint32_t commands[64]{};
    cb_words[0] = reinterpret_cast<u64>(commands);
    cb_words[1] = reinterpret_cast<u64>(commands + 64);
    cb_words[2] = reinterpret_cast<u64>(commands + 3);
    nested_fn = reinterpret_cast<IntFn>(image + 0x1075840);
    register u64 live_index asm("r12") = 0x12345678;
    asm volatile("" : "+r"(live_index));
    const Pair result = reinterpret_cast<IntFn>(image + 0x10753c0)(cb_words, 2, 3, 4, 5, 6);
    asm volatile("" : "+r"(live_index));
    assert(live_index == 0x12345678 && observed_r12 == live_index);
    assert(result.a == 0x123456789abcdef0ull && result.b == 0xfedcba9876543210ull);
    assert(depth == 0 && !CurrentProducerContext(commands + 3).known);
    using FloatFn = double (*)(u64*,u64,u64,u64,u64,u64,double,double,double,double,double,double,double,double);
    assert(reinterpret_cast<FloatFn>(image + 0x1075480)(cb_words,2,3,4,5,6,1,2,3,4,5,6,7,8) == 36);
    for (std::size_t i = 0; i < Count; ++i) assert(*CounterAt(i) == 0);
    // Fail-closed guest memory and TLS isolation, using the same context implementation.
    frames[0] = {}; frames[0].index = Index(0x1075840); frames[0].cb = 1; depth = 1;
    errno = E2BIG;
    auto bad = CurrentProducerContext(commands);
    assert(!bad.known && bad.read_failed && errno == E2BIG);
    std::thread([&] { assert(!CurrentProducerContext(commands + 3).known); }).join();
    frames[0].cb = reinterpret_cast<u64>(cb_words);
    context_overflow = true;
    assert(!CurrentProducerContext(commands + 3).known);
    context_overflow = false; depth = 0;

    // Precisely indexed record association and negative controls; no record census or target
    // uniqueness inference. Place the verified call instruction at its actual image offset.
    assert(mprotect(image, image_size, PROT_READ|PROT_WRITE) == 0);
    const u64 caller = 0x11fe65f;
    image[caller - 5] = 0xe8;
    const auto displacement = std::int32_t(0x1075840 - caller);
    std::memcpy(image + caller - 4, &displacement, 4);
    u64 table[9]{};
    std::uint32_t records[2][3]{{3, 4 | (12 << 8), 0}, {3, 4 | (12 << 8), 0}};
    std::uint32_t payload[10]{1,2,3,4,5,6,7,8,9,10};
    table[4] = reinterpret_cast<u64>(records); table[5] = reinterpret_cast<u64>(payload); table[8] = 2;
    assert(mprotect(image, image_size, PROT_READ|PROT_EXEC) == 0);
    using PatcherFn = Pair (*)(u64*, u64*);
    const Pair through_parent = reinterpret_cast<PatcherFn>(image + 0x11fde00)(table, cb_words);
    assert(through_parent.a == result.a && through_parent.b == result.b && depth == 0);
    assert(captured.exact_record && captured.exact_record_index == 1 && captured.table == reinterpret_cast<u64>(table));
    assert(captured.record_type_matches && captured.record_target_matches);
    assert(mprotect(image, image_size, PROT_READ|PROT_WRITE) == 0);
    frames[0] = {}; frames[0].index = Index(0x11fde00);
    frames[0].cb = reinterpret_cast<u64>(table); frames[0].patch_cb = reinterpret_cast<u64>(cb_words);
    frames[0].ret = image_base + 123;
    frames[1] = {}; frames[1].index = Index(0x1075840); frames[1].cb = reinterpret_cast<u64>(cb_words);
    frames[1].ret = image_base + caller; frames[1].entry_r12 = 2; depth = 2;
    auto exact = CurrentProducerContext(commands + 3);
    assert(exact.known && exact.exact_record && exact.exact_record_index == 1);
    assert(exact.record[1] == (4 | (12 << 8)) && exact.payload_word_count == 10 && exact.target_word_count == 40);
    frames[1].entry_r12 = 3; assert(!CurrentProducerContext(commands + 3).exact_record);
    frames[1].entry_r12 = 2; records[1][1] = 4 | (6 << 8);
    auto wrong_type = CurrentProducerContext(commands + 3);
    assert(wrong_type.exact_record && !wrong_type.record_type_matches && wrong_type.record[1] == (4 | (6 << 8)));
    records[1][1] = 4 | (12 << 8); records[1][0] = 4;
    auto wrong_target = CurrentProducerContext(commands + 3);
    assert(wrong_target.exact_record && !wrong_target.record_target_matches && wrong_target.record_target == reinterpret_cast<u64>(commands + 4));
    records[1][0] = 3; frames[0].patch_cb++;
    assert(!CurrentProducerContext(commands + 3).exact_record);
    frames[0].patch_cb--; image[caller - 5] = 0x90;
    assert(!CurrentProducerContext(commands + 3).exact_record);
    image[caller - 5] = 0xe8; table[4] = 1;
    assert(CurrentProducerContext(commands + 3).read_failed);
    table[4] = reinterpret_cast<u64>(records); table[5] = 1;
    auto unreadable_payload = CurrentProducerContext(commands + 3);
    assert(unreadable_payload.exact_record && unreadable_payload.read_failed && !unreadable_payload.payload_word_count);
    depth = 0;
}
} // namespace
int main() { Run(); std::puts("gnm context CPU tests passed (actual generated entry/exit stubs)"); }
