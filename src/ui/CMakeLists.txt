add_library(TasrovyUI STATIC
        UI.h
        UI.cpp
)

target_include_directories(TasrovyUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyUI PUBLIC
        imgui::imgui
        volk::volk
)
