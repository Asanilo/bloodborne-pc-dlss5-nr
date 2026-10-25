// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dlssg_gpu_contract.h"

namespace DlssgGpu {
// Native-only generation selection. The existing Wine wire count stays unchanged.
inline unsigned RequestedMultiplier(const J& config,unsigned requested){
    const auto initial=config.contains("multiplier")?U(config,"multiplier"):2;
    Need(initial>=2&&initial<=6&&requested>=2&&requested<=6,"bounded requested multiplier");
    if(!config.contains("allowed_multipliers"))return unsigned(initial);
    const auto& allowed=config.at("allowed_multipliers");
    Need(allowed.is_array()&&(allowed.size()==1||allowed.size()==5),"sealed multiplier policy");
    bool found=false;
    for(unsigned i=0;i<allowed.size();++i){
        const auto& value=allowed[i];Need(value.is_number_integer(),"integer allowed multiplier");
        Need(value==J(allowed.size()==1?initial:i+2),"exact bounded multiplier policy");
        found=found||value==requested;
    }
    return found?requested:unsigned(initial);
}
}
