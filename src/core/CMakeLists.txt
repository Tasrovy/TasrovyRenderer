add_executable(TasrovyCore
        main.cpp
)

target_include_directories(TasrovyCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyCore PUBLIC
        TasrovyWindow
        TasrovyUI
        TasrovyRHI
        TasrovyFileSystem
        TasrovyRender
        TasrovyLog
        TasrovyExt
        imgui
)
