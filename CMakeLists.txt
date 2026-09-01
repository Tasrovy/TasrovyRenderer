cmake_minimum_required(VERSION 3.15)
project(TasrovyRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)

set(TASROVY_RHI_BACKEND "Vulkan" CACHE STRING "Graphics backend")
set_property(CACHE TASROVY_RHI_BACKEND PROPERTY STRINGS Vulkan D3D12)

if(TASROVY_RHI_BACKEND STREQUAL "Vulkan")
    add_compile_definitions(TASROVY_API_VULKAN=1)
    find_package(volk CONFIG REQUIRED)
elseif(TASROVY_RHI_BACKEND STREQUAL "D3D12")
    message(FATAL_ERROR "The D3D12 backend has not been implemented yet")
else()
    message(FATAL_ERROR "Unsupported RHI backend: ${TASROVY_RHI_BACKEND}")
endif()

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

# imgui: built from source in third_party/
set(TASROVY_IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
)
if(TASROVY_RHI_BACKEND STREQUAL "Vulkan")
    list(APPEND TASROVY_IMGUI_SOURCES
        third_party/imgui/backends/imgui_impl_vulkan.cpp)
endif()
add_library(imgui STATIC ${TASROVY_IMGUI_SOURCES})
if(TASROVY_RHI_BACKEND STREQUAL "Vulkan")
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
    target_link_libraries(imgui PUBLIC volk::volk)
endif()
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_link_libraries(imgui PUBLIC glfw)

add_subdirectory(src/ext)
add_subdirectory(src/log)
add_subdirectory(src/base)
add_subdirectory(src/filesystem)
add_subdirectory(src/window)
add_subdirectory(src/render)
add_subdirectory(src/assets)
add_subdirectory(src/RHI)
add_subdirectory(src/ui)
add_subdirectory(src/renderer)
add_subdirectory(src/tools)
add_subdirectory(src/core)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT TasrovyCore)
