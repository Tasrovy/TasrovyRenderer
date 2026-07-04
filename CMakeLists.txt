cmake_minimum_required(VERSION 3.15)
project(TasrovyRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(volk CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

add_subdirectory(src/ext)
add_subdirectory(src/log)
add_subdirectory(src/window)
add_subdirectory(src/ui)
add_subdirectory(src/RHI)
add_subdirectory(src/filesystem)
add_subdirectory(src/render)
add_subdirectory(src/core)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TasrovyApp)