#include "UI.h"

#include "UIBackend.h"
#include "../RHI/Device.h"

#include <imgui.h>
#include <stdexcept>

namespace Tasrovy::UI {

UIOverlay::UIOverlay(const CreateInfo& info)
    : _window(info.window) {
    if (!_window || !info.device) {
        throw std::invalid_argument(
            "UIOverlay requires a window and an RHI device");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    _frameMutex = std::make_shared<std::mutex>();
    _backend = createUIBackend(*_window, *info.device, _frameMutex);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
}

UIOverlay::~UIOverlay() {
    _backend.reset();
    ImGui::DestroyContext();
}

uint64_t UIOverlay::beginFrame(
    uint32_t framebufferWidth,
    uint32_t framebufferHeight) {
    if (framebufferWidth == 0 || framebufferHeight == 0) return 0;
    // Rebuild only after the renderer has acquired the published snapshot at
    // least once. This ties ImGui DeltaTime and interaction updates to actual
    // rendered UI frames instead of the unrestricted GLFW event-loop rate.
    const uint64_t published =
        _publishedFrameToken.load(std::memory_order_acquire);
    if (published != 0 &&
        !_frameUpdateRequested.exchange(false, std::memory_order_acq_rel)) {
        return published;
    }
    std::scoped_lock lock(*_frameMutex);
    ImGui::GetIO().DisplaySize = ImVec2(
        static_cast<float>(framebufferWidth),
        static_cast<float>(framebufferHeight));
    _backend->newFrame();
    ImGui::NewFrame();
    if (_drawCallback) _drawCallback();
    ImGui::Render();
    const uint64_t frameToken = _backend->captureFrame();
    const uint64_t replacedToken =
        _publishedFrameToken.exchange(frameToken, std::memory_order_acq_rel);
    _frameReferences.try_emplace(frameToken, 0u);
    const auto replaced = _frameReferences.find(replacedToken);
    if (replacedToken != 0 && replaced != _frameReferences.end() &&
        replaced->second == 0) {
        _backend->discardFrame(replacedToken);
        _frameReferences.erase(replaced);
    }
    return frameToken;
}

uint64_t UIOverlay::acquireFrame() {
    std::scoped_lock lock(*_frameMutex);
    const uint64_t frameToken =
        _publishedFrameToken.load(std::memory_order_acquire);
    if (frameToken == 0) return 0;
    ++_frameReferences[frameToken];
    _frameUpdateRequested.store(true, std::memory_order_release);
    return frameToken;
}

void UIOverlay::releaseFrame(uint64_t frameToken) {
    if (frameToken == 0) return;
    std::scoped_lock lock(*_frameMutex);
    const auto found = _frameReferences.find(frameToken);
    if (found == _frameReferences.end()) return;
    if (found->second > 0) --found->second;
    if (found->second == 0 && frameToken !=
        _publishedFrameToken.load(std::memory_order_acquire)) {
        _backend->discardFrame(frameToken);
        _frameReferences.erase(found);
    }
}

Tasrovy::RHI::GraphicsAPI UIOverlay::getGraphicsAPI() const {
    return _backend->getGraphicsAPI();
}

void* UIOverlay::getBackendImplementation() {
    return _backend->getRenderBackend();
}

} // namespace Tasrovy::UI
