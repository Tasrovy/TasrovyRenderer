add_library(TasrovyLog STATIC
        Logger.hpp
        Logger.cpp
)

target_include_directories(TasrovyLog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyLog PUBLIC
        spdlog::spdlog
)
