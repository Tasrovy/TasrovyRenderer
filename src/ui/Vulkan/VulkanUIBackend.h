#pragma once

#include "../UIBackend.h"
#include "../../RHI/Vulkan/VulkanRenderOverlayBackend.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

struct ImDrawData;

namespace Tasrovy::UI {

class VulkanUIBackend final
    : public IUIBackend,
      public Tasrovy::RHI::Vulkan::VulkanRenderOverlayBackend {
public:
    VulkanUIBackend(
        GLFWwindow& window, Tasrovy::RHI::Device& device,
        std::shared_ptr<std::mutex> frameMutex);
    ~VulkanUIBackend() override;

    void newFrame() override;
    uint64_t captureFrame() override;
    void discardFrame(uint64_t frameToken) override;
    Tasrovy::RHI::GraphicsAPI getGraphicsAPI() const override;
    void* getRenderBackend() override;
    void recordDrawData(
        VkCommandBuffer commandBuffer, uint64_t frameToken) override;

private:
    struct CapturedFrame;
    uintptr_t device_ = 0;
    uintptr_t descriptorPool_ = 0;
    std::shared_ptr<std::mutex> frameMutex_;
    std::unordered_map<uint64_t, std::unique_ptr<CapturedFrame>> frames_;
    uint64_t nextFrameToken_ = 1;
};

} // namespace Tasrovy::UI
