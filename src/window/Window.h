#pragma once
#include <volk.h>
#include <string>
#include <vector>
#include <functional>

struct GLFWwindow;

struct WindowInfo {
    int width;
    int height;
    std::string title;
};

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    GLFWwindow* getHandle() const { return _window; }

    bool shouldClose() const;
    void pollEvents();
    void waitEvents();

    int getWidth() const { return _width; }
    int getHeight() const { return _height; }

    bool wasResized() const { return _framebufferResized; }
    void resetResizedFlag() { _framebufferResized = false; }

    const WindowInfo& getInfo() const { return _info; }

    // Vulkan integration helpers
    std::vector<const char*> getRequiredVulkanExtensions() const;
    VkSurfaceKHR createVulkanSurface(VkInstance instance) const;

private:
    void updateFramebufferSize();

    GLFWwindow* _window = nullptr;
    WindowInfo _info;
    int _width = 0;
    int _height = 0;
    bool _framebufferResized = false;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};
