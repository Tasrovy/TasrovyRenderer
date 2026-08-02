#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <cstdint>
#include <mutex>

struct GLFWwindow;

namespace Tasrovy::Windowing {

struct WindowInfo {
    int width;
    int height;
    std::string title;
};

struct FramebufferState {
    int width = 0;
    int height = 0;
    uint64_t resizeGeneration = 0;
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

    int getWidth() const;
    int getHeight() const;
    FramebufferState getFramebufferState() const;

    const WindowInfo& getInfo() const { return _info; }

private:
    void updateFramebufferSize();

    GLFWwindow* _window = nullptr;
    WindowInfo _info;
    mutable std::mutex _framebufferMutex;
    int _width = 0;
    int _height = 0;
    std::atomic<uint64_t> _resizeGeneration{0};

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace Tasrovy::Windowing
