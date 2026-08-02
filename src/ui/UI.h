#pragma once
#include <cstdint>
#include <functional>
#include "RenderOverlay.h"

struct GLFWwindow;

namespace Tasrovy::UI {

class UIOverlay final : public Tasrovy::RHI::RenderOverlay {
public:
    struct CreateInfo {
        GLFWwindow* window = nullptr;
        uint64_t instance = 0;
        uint64_t physicalDevice = 0;
        uint64_t device = 0;
        uint64_t graphicsQueue = 0;
        uint32_t queueFamily = 0;
        uint32_t minImageCount = 3;
        uint32_t imageCount = 3;
        uint32_t msaaSamples = 1;
        uint32_t colorFormat = 0;
    };

    UIOverlay(const CreateInfo& info);
    ~UIOverlay();

    UIOverlay(const UIOverlay&) = delete;
    UIOverlay& operator=(const UIOverlay&) = delete;

    using DrawCallback = std::function<void()>;
    void setDrawCallback(DrawCallback cb) { _drawCallback = std::move(cb); }

    bool beginFrame(uint32_t framebufferWidth, uint32_t framebufferHeight);
    void renderDrawData(uint64_t nativeCommandBuffer);
    void recordDrawData(uint64_t nativeCommandBuffer) override {
        renderDrawData(nativeCommandBuffer);
    }

private:
    GLFWwindow* _window = nullptr;
    uint64_t _device = 0;
    uint64_t _descriptorPool = 0;
    uint32_t _colorFormat = 0;
    DrawCallback _drawCallback;
};

} // namespace Tasrovy::UI
