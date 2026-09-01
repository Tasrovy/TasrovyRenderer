#pragma once

#include <cstdint>
#include "RHITypes.h"

namespace Tasrovy::RHI {

class RenderOverlay {
public:
    virtual ~RenderOverlay() = default;
    virtual GraphicsAPI getGraphicsAPI() const = 0;
    virtual void* getBackendImplementation() = 0;
};

} // namespace Tasrovy::RHI
