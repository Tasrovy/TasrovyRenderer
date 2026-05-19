#include <volk.h>       // 必须在 GLFW 之前
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

int main() {
    // 1. 测试 spdlog (日志库)
    spdlog::set_pattern("[%^%l%$] %v");
    spdlog::info("Starting TasrovyRenderer...");

    // 2. 初始化 GLFW (窗口库)
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
        return -1;
    }

    // 3. 初始化 Volk (Vulkan 加载器)
    if (volkInitialize() != VK_SUCCESS) {
        spdlog::error("Failed to initialize Volk! Is Vulkan SDK installed?");
        return -1;
    }
    spdlog::info("Volk & GLFW initialized successfully.");

    // 4. 创建窗口 (告诉 GLFW 不要创建 OpenGL 上下文)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Tasrovy Renderer - Dev", nullptr, nullptr);

    if (!window) {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }

    // 5. 主循环
    spdlog::info("Entering main loop...");
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        // 这里以后就是渲染代码
    }

    // 6. 清理
    spdlog::info("Shutting down TasrovyRenderer...");
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}