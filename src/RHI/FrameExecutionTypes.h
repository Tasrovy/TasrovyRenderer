#pragma once

#include "Device.h"

#include <cstdint>

namespace Tasrovy::RHI {

struct FrameResourceConfig {
    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;
    uint32_t internalWidth = 0;
    uint32_t internalHeight = 0;
    uint32_t framesInFlight = 0;
    uint32_t virtualShadowPageSize = 2048;
    uint32_t virtualShadowPageCount = 4;
    Device::ResourceScope sceneScope = 0;
    Device::ResourceScope displayScope = 0;
};

struct ResolvedTextureInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    RenderTextureFormat format = RenderTextureFormat::RGBA8Unorm;
    bool external = false;
};

} // namespace Tasrovy::RHI
