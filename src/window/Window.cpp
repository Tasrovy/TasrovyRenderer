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
    glfwGetFramebufferSize(_window, &_width, &_height);
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->_width = width;
        self->_height = height;
        self->_framebufferResized = true;
    }
}

} // namespace Tasrovy::Windowing
