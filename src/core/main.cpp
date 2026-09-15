#include "SceneSerializer.h"

#include "DeferredPipeline.h"
#include "Logger.hpp"
#include "Scene.h"
#include "SceneRenderer.h"
#include "Window.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

void setWorkingDirectoryToProjectRoot()
{
    std::error_code ec;
    fs::path directory = fs::current_path(ec);
    if (ec) {
        LOG_WARN("Failed to query current working directory: {}", ec.message());
        return;
    }

    while (!directory.empty()) {
        if (fs::exists(directory / "res" / "Shaders", ec) && !ec) {
            fs::current_path(directory, ec);
            if (ec) {
                LOG_WARN(
                    "Failed to switch working directory to '{}': {}",
                    directory.string(), ec.message());
            } else {
                LOG_INFO(
                    "Working directory set to project root '{}'",
                    directory.string());
            }
            return;
        }

        const fs::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }

    LOG_WARN(
        "Could not locate the project root from '{}'",
        fs::current_path().string());
}

bool isSceneFile(const fs::path& path)
{
    return path.filename().string().ends_with(".scene.json");
}

std::vector<Tasrovy::Core::SceneMetadata> discoverScenes(
    const fs::path& resourceRoot)
{
    std::vector<Tasrovy::Core::SceneMetadata> scenes;
    std::error_code ec;
    if (!fs::is_directory(resourceRoot, ec) || ec) {
        LOG_ERROR(
            "Scene discovery root '{}' does not exist",
            resourceRoot.string());
        return scenes;
    }

    fs::recursive_directory_iterator iterator(
        resourceRoot,
        fs::directory_options::skip_permission_denied,
        ec);
    const fs::recursive_directory_iterator end;
    while (!ec && iterator != end) {
        if (iterator->is_regular_file(ec) && !ec &&
            isSceneFile(iterator->path())) {
            Tasrovy::Core::SceneMetadata metadata;
            if (Tasrovy::Core::SceneSerializer::inspect(
                    iterator->path(), metadata)) {
                scenes.push_back(std::move(metadata));
            }
        }
        iterator.increment(ec);
    }
    if (ec) {
        LOG_WARN("Scene discovery stopped early: {}", ec.message());
    }

    std::ranges::sort(scenes, {}, [](const auto& scene) {
        return scene.path.generic_string();
    });
    return scenes;
}

std::optional<fs::path> selectScene(
    const std::vector<Tasrovy::Core::SceneMetadata>& scenes,
    int argc,
    char* argv[])
{
    if (scenes.empty()) {
        LOG_ERROR("No '*.scene.json' files were found under 'res'");
        return std::nullopt;
    }

    if (argc > 1) {
        const fs::path requested = fs::path(argv[1]).lexically_normal();
        const auto found = std::ranges::find_if(
            scenes,
            [&requested](const auto& candidate) {
                return candidate.path == requested ||
                    candidate.path.filename() == requested ||
                    candidate.path.stem() == requested ||
                    candidate.name == requested.string();
            });
        if (found == scenes.end()) {
            LOG_ERROR(
                "Requested scene '{}' is not present in the res scene catalog",
                requested.string());
            return std::nullopt;
        }
        return found->path;
    }

    if (scenes.size() == 1) {
        LOG_INFO(
            "Discovered one scene: '{}' ({})",
            scenes.front().name, scenes.front().path.string());
        return scenes.front().path;
    }

    std::cout << "Available scenes:\n";
    for (size_t index = 0; index < scenes.size(); ++index) {
        std::cout << "  " << index + 1 << ". "
                  << scenes[index].name << "  ["
                  << scenes[index].path.generic_string() << "]\n";
    }
    std::cout << "Select a scene [1-" << scenes.size() << "]: "
              << std::flush;

    std::string input;
    if (!std::getline(std::cin, input)) {
        LOG_ERROR(
            "No scene was selected; pass a catalog path on the command line");
        return std::nullopt;
    }
    try {
        size_t parsedCharacters = 0;
        const size_t selection = std::stoull(input, &parsedCharacters);
        if (parsedCharacters != input.size() ||
            selection == 0 || selection > scenes.size()) {
            throw std::out_of_range("scene selection");
        }
        return scenes[selection - 1].path;
    } catch (const std::exception&) {
        LOG_ERROR("Invalid scene selection '{}'", input);
        return std::nullopt;
    }
}

} // namespace

int main(int argc, char* argv[])
{
    Tasrovy::Log::Logger::Init();
    setWorkingDirectoryToProjectRoot();

    const auto scenePath = selectScene(discoverScenes("res"), argc, argv);
    if (!scenePath) {
        return EXIT_FAILURE;
    }

    Tasrovy::Windowing::Window window(1280, 800, "TasrovyRenderer");
    const float cameraAspect =
        static_cast<float>(window.getWidth()) /
        static_cast<float>(window.getHeight());

    Tasrovy::Core::SceneArchive sceneArchive;
    if (!Tasrovy::Core::SceneSerializer::load(
            *scenePath, cameraAspect, sceneArchive) ||
        !sceneArchive.scene) {
        LOG_ERROR(
            "Application startup aborted: scene '{}' could not be loaded",
            scenePath->string());
        return EXIT_FAILURE;
    }

    auto pipeline = Tasrovy::Render::DeferredPipeline::create();
    pipeline->GenPass(sceneArchive.scene);

    Tasrovy::Renderer::SceneRenderer renderer(window, 4);
    renderer.setScene(sceneArchive.scene);
    renderer.setPipeline(pipeline);
    renderer.start();

    LOG_INFO(
        "Scene '{}' loaded from '{}' and submitted to the renderer",
        sceneArchive.scene->getName(), scenePath->string());
    while (!window.shouldClose()) {
        window.pollEvents();
        if (!window.shouldClose()) {
            renderer.buildUIFrame();
        }
    }

    renderer.stop();
    LOG_INFO("Application exiting");
    return EXIT_SUCCESS;
}
