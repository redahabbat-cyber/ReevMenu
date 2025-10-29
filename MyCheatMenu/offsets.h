#pragma once
#include <cstdint>

namespace offsets {
    // Oct 29, 2025 values - full offsets (load + base in MainThread)
    extern uintptr_t dwLocalPlayerPawn;
    extern uintptr_t dwEntityList;
    extern uintptr_t dwViewMatrix;
    constexpr auto m_iHealth = 0x334;      // Corrected: C_BaseEntity::m_iHealth
    constexpr auto m_vecOrigin = 0x1224;   // Corrected: C_BasePlayerPawn::m_vOldOrigin (for ESP)
    constexpr auto m_iTeamNum = 0x3CB;     // Corrected: C_BaseEntity::m_iTeamNum
    // Add more as needed
}