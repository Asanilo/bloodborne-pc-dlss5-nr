// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace AmdGpu::Pm4RegisterGuard {
// DWORD address ranges, end-exclusive. Primary AMD CI definitions:
// linux/drivers/gpu/drm/amd/amdgpu/cikd.h PACKET3_SET_{SH,CONTEXT,UCONFIG}_REG_{START,END}.
// https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/cikd.h
// PM4CmdSetData in pm4_cmds.h: offset[15:0], reserved[27:16], CI index[31:28].
inline constexpr std::uint32_t ConfigStart=0x2000,ConfigEnd=0x2c00;
inline constexpr std::uint32_t ShStart=0x2c00,ShEnd=0x3000;
inline constexpr std::uint32_t ContextStart=0xa000,ContextEnd=0xa400;
inline constexpr std::uint32_t UconfigStart=0xc000,UconfigEnd=0xc400;
enum class Error {None,Truncated,MissingValue,ReservedOffset,OutsideBank};
struct Result {
 Error error=Error::None;
 std::size_t words=0;
 std::uint32_t opcode=0,offset=0;
 explicit constexpr operator bool()const noexcept{return error==Error::None;}
};
inline constexpr const char* Reason(Error e) noexcept {
 switch(e){case Error::None:return "valid";case Error::Truncated:return "packet_exceeds_actual_remaining_words";
 case Error::MissingValue:return "register_packet_requires_offset_and_value";case Error::ReservedOffset:return "reserved_register_offset_bits";
 case Error::OutsideBank:return "register_write_outside_opcode_bank";}
 return "unknown";
}
// Bounds for the per-queue ComputeProgram subrange, independent of the larger SH bank.
inline constexpr bool FitsWindow(std::uint32_t offset, std::size_t values,
                                 std::uint32_t first, std::size_t count) noexcept {
 return offset>=first && std::size_t(offset-first)<count && values<=count-(offset-first);
}
inline constexpr Result Validate(std::span<const std::uint32_t> remaining) noexcept {
 if(remaining.empty())return {Error::Truncated};
 const auto header=remaining[0];const auto op=(header>>8)&0xffu;
 if((header>>30)!=3u||(op!=0x68u&&op!=0x76u&&op!=0x69u&&op!=0x79u))return {};
 // Derive from raw count without PM4Type3Header::NumWords' 14-bit wrap at 0x3fff.
 const std::size_t words=((header>>16)&0x3fffu)+2u;
 Result r{Error::None,words,op,0};
 if(words>remaining.size()){r.error=Error::Truncated;return r;}
 if(words<3u){r.error=Error::MissingValue;return r;}
 const auto raw=remaining[1];r.offset=raw&0xffffu;
 // Preserve CI's existing context/uconfig index nibble; plain SH has no index.
 const auto reserved=(op==0x76u||op==0x68u)?0xffff0000u:0x0fff0000u;
 if(raw&reserved){r.error=Error::ReservedOffset;return r;}
 const std::uint32_t bank=op==0x68u?ConfigEnd-ConfigStart:op==0x76u?ShEnd-ShStart:op==0x69u?ContextEnd-ContextStart:UconfigEnd-UconfigStart;
 const auto values=words-2u;
 if(r.offset>=bank||values>bank-r.offset){r.error=Error::OutsideBank;return r;}
 return r;
}
}
