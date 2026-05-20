cmake_minimum_required(VERSION 3.15)
project(TasrovyRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 寻找全局第三方依赖
find_package(volk CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Vulkan REQUIRED) # 引入 Vulkan SDK 依赖

# 2. 【最关键的一步】告诉 CMake 去引入你的子项目文件夹
# 注意：如果你的 core 文件夹还没建好，可以先在 add_subdirectory(src/core) 前面加 # 注释掉
# add_subdirectory(src/core) 
add_subdirectory(src/render)

# 3. 声明你的主程序
add_executable(TasrovyApp main.cpp)

# 4. 让主程序链接你的渲染子项目
target_link_libraries(TasrovyApp PRIVATE 
    TasrovyRender
)

# 5. 设置启动项目为 TasrovyApp
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TasrovyApp)