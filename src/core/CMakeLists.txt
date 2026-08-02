add_executable(TasrovyCore
        main.cpp
        SceneSerializer.cpp
        SceneSerializer.h
)

find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(TasrovyCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyCore PUBLIC
        TasrovyWindow
        TasrovyUI
        TasrovyRendererRuntime
        TasrovyRHI
        TasrovyFileSystem
        TasrovyAssets
        TasrovyRender
        TasrovyLog
        TasrovyExt
        imgui
        nlohmann_json::nlohmann_json
)
