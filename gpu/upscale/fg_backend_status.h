// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <chrono>
#include <atomic>
namespace Vulkan::FgBackendStatus {
inline std::atomic<unsigned> active_multiplier{2};
inline bool Is(const char* value,const char* expected){return value&&std::strcmp(value,expected)==0;}
inline const char* Selected(){
    if(const auto* value=std::getenv("BB_FRAME_GEN_BACKEND"))return value;
    if(Is(std::getenv("NVPRESENT_ENABLE_SMOOTH_MOTION"),"1"))return "smooth-motion";
    return Is(std::getenv("BB_FRAME_GEN"),"1")?"fsr31":"off";
}
inline const char* Name(){
    const auto* v=Selected();
    if(Is(v,"off"))return "Off";
    if(Is(v,"fsr31"))return "AMD FSR 3.1 2X";
    if(Is(v,"fsr316"))return "AMD FSR 3.1.6 2X";
    if(Is(v,"dlssg")){
        static constexpr const char* labels[]={"NVIDIA DLSS FG 2X","NVIDIA DLSS MFG 3X","NVIDIA DLSS MFG 4X","NVIDIA DLSS MFG 5X","NVIDIA DLSS MFG 6X"};
        const unsigned count=active_multiplier.load();return labels[count>=2&&count<=6?count-2:0];
    }
    if(Is(v,"smooth-motion"))return "NVIDIA Smooth Motion";
    if(Is(v,"dlssg-capture"))return "DLSS FG input diagnostic";
    return "Unknown / unavailable";
}
inline bool Application(){const auto* v=Selected();return Is(v,"fsr31")||Is(v,"fsr316")||Is(v,"dlssg")||Is(v,"dlssg-capture");}
inline const char* SmoothMotionStatus(){
    // Loader mappings plus driver's own activation report; never infer scanout counts.
    static auto last=std::chrono::steady_clock::time_point{};
    static const char* status="Driver activation pending / unverified; restart required.";
    const auto now=std::chrono::steady_clock::now();
    if(now-last<std::chrono::seconds(1))return status;
    last=now;
    std::ifstream maps("/proc/self/maps");std::string line;bool loaded=false;
    while(std::getline(maps,line))if(line.find("libnvidia-present.so")!=std::string::npos)loaded=true;
    std::string report;const char* path=std::getenv("NVPRESENT_LOG_FILE");
    if(path){std::ifstream f(path);report.resize(65536);f.read(report.data(),report.size());report.resize(f.gcount());}
    const bool supported=report.find("System support: 1. Device support: 1")!=std::string::npos;
    const auto enabled=report.find("Smooth Motion enabled");
    const bool on=enabled!=std::string::npos&&report.substr(enabled,64).find("[1]")!=std::string::npos;
    if(loaded&&supported&&on)status="Driver layer loaded and reports enabled. Display-frame count unverified. Restart to change backend.";
    else if(report.find("System support: 0")!=std::string::npos||report.find("Device support: 0")!=std::string::npos)
        status="Smooth Motion unavailable: driver reports unsupported. Restart with another backend.";
    else status="Smooth Motion requested; driver activation unverified. Restart required; application FG is off.";
    return status;
}
}
