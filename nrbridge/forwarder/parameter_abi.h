#pragma once
#include <cstdio>
#include <cstring>
#include <cstdint>
// Reuse the explicit vendor vtable rule documented/used by the stable forwarder:
// OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp (setters and scaling ratio getter).
// Do not use LLVM-MinGW's ordering of overloaded C++ virtual methods against an
// NVIDIA/MSVC-owned object. Every candidate is verified by an exact round trip.
inline int nr_probe_get_float(void* params, const char* key, int setter, float* value)
{
    using GetUnknown = int(__thiscall*)(void*, const char*, void*);
    void** vt = *reinterpret_cast<void***>(params);
    // All 8 getter overloads return at most one primitive/pointer (8 bytes).
    // Give unknown getters 64 aligned bytes, plus red zones on both sides.
    // A double/pointer write must never count as a successful float round trip.
    alignas(16) unsigned char storage[128];
    std::memset(storage, 0xA5, sizeof(storage));
    unsigned char* payload = storage + 32;
    const int result = reinterpret_cast<GetUnknown>(vt[setter + 8])(params, key, payload);
    bool guards = true, floatOnly = true;
    for (unsigned i = 0; i < 32; ++i)
        guards &= storage[i] == 0xA5;
    for (unsigned i = 96; i < 128; ++i)
        guards &= storage[i] == 0xA5;
    for (unsigned i = 4; i < 64; ++i)
        floatOnly &= payload[i] == 0xA5;
    std::memcpy(value, payload, 4);
    uint64_t bits = 0;
    std::memcpy(&bits, payload, 8);
    std::printf("FLOAT_PROBE setter=%d getter=%d result=0x%08x value=%g bits64=%016llx canary=%d float_width=%d\n",
                setter, setter + 8, result, *value, (unsigned long long) bits, guards, floatOnly);
    // Canary violations and 8-byte writes are rejected even when low32 matches.
    return guards && floatOnly ? result : int(0xBAD00000u);
}

inline bool nr_discover_float(void* params, void(__cdecl* probe)(void*, const char*, float, int),
                              void(__cdecl* setSlot)(int))
{
    for (int s : { 1, 2, 5, 6, 7, 4, 3, 0 })
    {
        bool valid = true;
        for (float expected : { 0.375f, 0.625f })
        {
            float value = 0;
            probe(params, "DLSSNR.OptiScalerFloatProbe", expected, s);
            if (nr_probe_get_float(params, "DLSSNR.OptiScalerFloatProbe", s, &value) != 1 || value != expected)
            {
                valid = false;
                break;
            }
        }
        if (valid)
        {
            setSlot(s);
            std::printf("FLOAT_SLOT %d confirmed_getter=%d exact_roundtrips=2 canary=1 float_width=1\n", s, s + 8);
            return true;
        }
    }
    return false;
}
