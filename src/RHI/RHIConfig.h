#pragma once

// ========================================
// Graphics API Selection
// ========================================
// The build system defines exactly one backend. Do not select an API in a
// public header: doing so would make every consumer implicitly Vulkan-only.

// ========================================
// Validation
// ========================================
#if defined(TASROVY_API_VULKAN) + defined(TASROVY_API_D3D12) != 1
    #error "Exactly one graphics API must be defined in RHIConfig.h"
#endif

// ========================================
// API-specific macros
// ========================================
#ifdef TASROVY_API_VULKAN
    #define TASROVY_API_NAME "Vulkan"
#elif defined(TASROVY_API_D3D12)
    #define TASROVY_API_NAME "D3D12"
#endif
