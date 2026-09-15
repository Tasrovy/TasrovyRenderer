#pragma once
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include "RenderOverlay.h"

struct GLFWwindow;

namespace Tasrovy::RHI { class Device; }

namespace Tasrovy::UI {

class IUIBackend;

class UIOverlay final : public Tasrovy::RHI::RenderOverlay {
public:
    struct CreateInfo {
        GLFWwindow* window = nullptr;
        Tasrovy::RHI::Device* device = nullptr;
    };

    UIOverlay(const CreateInfo& info);
    ~UIOverlay();

    UIOverlay(const UIOverlay&) = delete;
    UIOverlay& operator=(const UIOverlay&) = delete;

    using DrawCallback = std::function<void()>;
    void setDrawCallback(DrawCallback cb) { _drawCallback = std::move(cb); }

    // Returns an immutable backend frame token. Token 0 means no overlay.
    uint64_t beginFrame(uint32_t framebufferWidth, uint32_t framebufferHeight);
    uint64_t acquireFrame();
    void releaseFrame(uint64_t frameToken);
    Tasrovy::RHI::GraphicsAPI getGraphicsAPI() const override;
    void* getBackendImplementation() override;

private:
    GLFWwindow* _window = nullptr;
    std::unique_ptr<IUIBackend> _backend;
    std::shared_ptr<std::mutex> _frameMutex;
    std::atomic<uint64_t> _publishedFrameToken{0};
    std::atomic<bool> _frameUpdateRequested{true};
    std::unordered_map<uint64_t, uint32_t> _frameReferences;
    DrawCallback _drawCallback;
};

} // namespace Tasrovy::UI
