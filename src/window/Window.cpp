#include "Window.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace Tasrovy::Windowing {

Window::Window(int width, int height, const std::string& title)
    : _info{ width, height, title }
{
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW!");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    _window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window!");
    }
    glfwSetWindowUserPointer(_window, this);
    glfwSetFramebufferSizeCallback(_window, framebufferResizeCallback);
    updateFramebufferSize();
}

Window::~Window() {
    if (_window) {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(_window);
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::waitEvents() {
    glfwWaitEvents();
}

int Window::getWidth() const {
    std::lock_guard<std::mutex> lock(_framebufferMutex);
    return _width;
}

int Window::getHeight() const {
    std::lock_guard<std::mutex> lock(_framebufferMutex);
    return _height;
}

FramebufferState Window::getFramebufferState() const {
    std::lock_guard<std::mutex> lock(_framebufferMutex);
    return {_width, _height, _resizeGeneration.load(std::memory_order_relaxed)};
}

std::vector<const char*> Window::getRequiredVulkanExtensions() const {
    uint32_t count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    return std::vector<const char*>(extensions, extensions + count);
}

VkSurfaceKHR Window::createVulkanSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, _window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
    return surface;
}

void Window::updateFramebufferSize() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(_window, &width, &height);
    std::lock_guard<std::mutex> lock(_framebufferMutex);
    _width = width;
    _height = height;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        std::lock_guard<std::mutex> lock(self->_framebufferMutex);
        self->_width = width;
        self->_height = height;
        self->_resizeGeneration.fetch_add(1, std::memory_order_release);
    }
}

} // namespace Tasrovy::Windowing
