// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader/info.h"
#include "shader/profile.h"
#include "shader/runtime_info.h"

namespace Shader {

// Matches the built-ins declared by SPIR-V DefineInputs/DefineVertexBlock.
// A draw without space retains its ordinary shader and camera-motion fallback.
inline bool MotionInterfaceFits(const Info& vs, const Info& fs,
                                const HwFragmentRuntimeInfo& fragment, const Profile& profile,
                                u32 vertex_limit, u32 fragment_limit) {
    using IR::Attribute;
    // A guest export at the injected attachment is a color, never an object vector.
    if (fs.mrt_mask & (1u << MotionVectors::Output))
        return false;
    const bool clip = vs.stores.GetAny(Attribute::ClipDistance);
    const bool emulated_clip = clip && profile.needs_clip_distance_emulation;
    for (u32 i = 0; i < IR::NumParams; ++i) {
        if (vs.stores.GetAny(Attribute::Param0 + i) &&
            i + u32(emulated_clip) >= MotionVectors::CurrentLocation)
            return false;
    }
    const u32 clip_input = u32(fragment.clip_distance_emulation);
    if (fragment.num_inputs < clip_input || fragment.num_inputs > fragment.inputs.size())
        return false;
    for (u32 i = 0; i < fragment.num_inputs - clip_input; ++i) {
        if (!fragment.inputs[i].IsDefault() &&
            fragment.inputs[i].param_index + u32(fragment.clip_distance_emulation) >=
                MotionVectors::CurrentLocation)
            return false;
    }
    u32 vertex_builtins = 4; // Position
    vertex_builtins += clip && !emulated_clip ? 8 : 0;
    vertex_builtins += vs.stores.GetAny(Attribute::CullDistance) ? 8 : 0;
    vertex_builtins += vs.stores.GetAny(Attribute::PointSize);
    vertex_builtins += vs.stores.GetAny(Attribute::RenderTargetIndex);
    vertex_builtins += vs.stores.GetAny(Attribute::ViewportIndex);

    const auto reads = [&](Attribute attr) { return fs.loads.GetAny(attr); };
    u32 fragment_builtins = 4; // Motion always declares FragCoord
    fragment_builtins += reads(Attribute::IsFrontFace);
    fragment_builtins += reads(Attribute::RenderTargetIndex);
    fragment_builtins += reads(Attribute::SampleIndex);
    fragment_builtins += reads(Attribute::IsHelperInvocation);
    fragment_builtins += reads(Attribute::SampleMask);
    if (profile.supports_amd_shader_explicit_vertex_parameter) {
        fragment_builtins += 2 * u32(reads(Attribute::BaryCoordSmooth));
        fragment_builtins += 3 * u32(reads(Attribute::BaryCoordPullModel));
        fragment_builtins += 2 * u32(reads(Attribute::BaryCoordSmoothCentroid));
        fragment_builtins += 2 * u32(reads(Attribute::BaryCoordSmoothSample));
        fragment_builtins += 2 * u32(reads(Attribute::BaryCoordNoPersp));
        fragment_builtins += 2 * u32(reads(Attribute::BaryCoordNoPerspSample));
    } else if (profile.supports_fragment_shader_barycentric) {
        const bool smooth = reads(Attribute::BaryCoordSmooth) ||
                            reads(Attribute::BaryCoordPullModel) ||
                            reads(Attribute::BaryCoordSmoothCentroid);
        const bool sample = reads(Attribute::BaryCoordSmoothSample);
        fragment_builtins += smooth || sample ? 3 : 0;
        // SampleId is added when SmoothSample creates the shared barycentric input.
        fragment_builtins += sample && !smooth && !reads(Attribute::SampleIndex);
        fragment_builtins += reads(Attribute::BaryCoordNoPersp) ? 3 : 0;
        fragment_builtins += reads(Attribute::BaryCoordNoPerspSample) ? 3 : 0;
    }
    constexpr u32 user_end = 4 * (MotionVectors::PreviousLocation + 1);
    return user_end + vertex_builtins <= vertex_limit &&
           user_end + fragment_builtins <= fragment_limit;
}

} // namespace Shader
