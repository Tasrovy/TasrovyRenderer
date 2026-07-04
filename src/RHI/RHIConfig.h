#pragma once

// ========================================
// Graphics API Selection
// ========================================
// Uncomment exactly ONE of the following to select the graphics API.

// #define TASROVY_USE_D3D12
// #define TASROVY_USE_OPENGL
// #define TASROVY_USE_METAL
#define TASROVY_USE_VULKAN

// ========================================
// Validation
// ========================================
#if defined(TASROVY_USE_VULKAN) + defined(TASROVY_USE_D3D12) + defined(TASROVY_USE_OPENGL) + defined(TASROVY_USE_METAL) != 1
    #error "Exactly one graphics API must be defined in RHIConfig.h"
#endif

// ========================================
// API-specific macros
// ========================================
#ifdef TASROVY_USE_VULKAN
    #define TASROVY_API_NAME "Vulkan"
    #define TASROVY_API_VULKAN 1
#elif defined(TASROVY_USE_D3D12)
    #define TASROVY_API_NAME "D3D12"
    #define TASROVY_API_D3D12 1
#elif defined(TASROVY_USE_OPENGL)
    #define TASROVY_API_NAME "OpenGL"
    #define TASROVY_API_OPENGL 1
#elif defined(TASROVY_USE_METAL)
    #define TASROVY_API_NAME "Metal"
    #define TASROVY_API_METAL 1
#endif
