#pragma once

#include "../RHI/RHITypes.h"

#include <cstdint>
#include <memory>
#include <mutex>

struct GLFWwindow;

namespace Tasrovy::RHI { class Device; }

namespace Tasrovy::UI {

class IUIBackend {
public:
    virtual ~IUIBackend() = default;
    virtual void newFrame() = 0;
    virtual uint64_t captureFrame() = 0;
    virtual void discardFrame(uint64_t frameToken) = 0;
    virtual Tasrovy::RHI::GraphicsAPI getGraphicsAPI() const = 0;
    virtual void* getRenderBackend() = 0;
};

std::unique_ptr<IUIBackend> createUIBackend(
    GLFWwindow& window, Tasrovy::RHI::Device& device,
    std::shared_ptr<std::mutex> frameMutex);

} // namespace Tasrovy::UI
