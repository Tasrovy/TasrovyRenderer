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
    std::scoped_lock lock(*_frameMutex);
    ImGui::GetIO().DisplaySize = ImVec2(
        static_cast<float>(framebufferWidth),
        static_cast<float>(framebufferHeight));
    _backend->newFrame();
    ImGui::NewFrame();
    if (_drawCallback) _drawCallback();
    ImGui::Render();
    return _backend->captureFrame();
}

void UIOverlay::discardFrame(uint64_t frameToken) {
    if (frameToken == 0) return;
    std::scoped_lock lock(*_frameMutex);
    _backend->discardFrame(frameToken);
}

Tasrovy::RHI::GraphicsAPI UIOverlay::getGraphicsAPI() const {
    return _backend->getGraphicsAPI();
}

void* UIOverlay::getBackendImplementation() {
    return _backend->getRenderBackend();
}

} // namespace Tasrovy::UI
