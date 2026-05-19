cmake_minimum_required(VERSION 3.15)

project(TasrovyRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(volk CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_executable(TasrovyApp main.cpp)

target_link_libraries(TasrovyApp PRIVATE 
    volk::volk 
    glfw 
    glm::glm 
    spdlog::spdlog
)