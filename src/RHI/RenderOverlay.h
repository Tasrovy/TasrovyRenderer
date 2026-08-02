#pragma once

#include <cstdint>

namespace Tasrovy::RHI {

class RenderOverlay {
public:
    virtual ~RenderOverlay() = default;
    virtual void recordDrawData(uint64_t nativeCommandBuffer) = 0;
};

} // namespace Tasrovy::RHI
