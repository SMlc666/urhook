#pragma once

#if defined(__aarch64__)
#include "ur/assembler/arm64-v8a/assembler.h"
namespace ur::assembler {
    using Assembler = arm64::AssemblerAArch64;
    
    // Forward enums to maintain API compatibility
    using Register = arm64::Register;
    using Condition = arm64::Condition;
    using NeonArrangement = arm64::NeonArrangement;
    using SystemRegister = arm64::SystemRegister;
    using BarrierOption = arm64::BarrierOption;
}
#else
#error "Unsupported architecture for assembler"
#endif
