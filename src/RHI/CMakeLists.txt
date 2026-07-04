cmake_minimum_required(VERSION 3.20)
project(TasrovyRHI)

add_subdirectory(Vulkan)

add_library(TasrovyRHI STATIC
        IBuffer.h
)

target_include_directories(TasrovyRHI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(TasrovyRHI PUBLIC
            TasrovyRHI_Vulkan
)