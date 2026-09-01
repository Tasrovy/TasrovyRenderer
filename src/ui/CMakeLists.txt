add_library(TasrovyUI STATIC
        UI.h
        UI.cpp
        UIBackend.h
        Components.h
)

if(TASROVY_RHI_BACKEND STREQUAL "Vulkan")
    target_sources(TasrovyUI PRIVATE
        Vulkan/VulkanUIBackend.cpp
        Vulkan/VulkanUIBackend.h)
endif()

target_include_directories(TasrovyUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyUI PUBLIC
        TasrovyRHI
        TasrovyLog
        imgui
)
if(TASROVY_RHI_BACKEND STREQUAL "Vulkan")
    target_link_libraries(TasrovyUI PRIVATE volk::volk)
endif()
