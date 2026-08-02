add_library(TasrovyUI STATIC
        UI.h
        UI.cpp
        Components.h
)

target_include_directories(TasrovyUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyUI PUBLIC
        TasrovyRHI
        TasrovyLog
        imgui
        volk::volk
)
