# glfw
find_package(glfw3 CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)

add_library(TasrovyWindow STATIC
        Window.h
        Window.cpp
)

target_include_directories(TasrovyWindow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyWindow PUBLIC
        glfw
        volk::volk
)
