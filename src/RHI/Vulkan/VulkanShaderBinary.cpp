#include "VulkanShaderBinary.h"

#include <filesystem>
#include <stdexcept>

namespace Tasrovy::RHI::Vulkan {

std::string resolveShaderBinary(const ShaderModuleDesc& shader) {
    if (shader.sourcePath.empty()) {
        return {};
    }

    const std::filesystem::path source(shader.sourcePath);
    if (source.extension() == ".spv") {
        throw std::invalid_argument(
            "Vulkan shader descriptions must reference HLSL source, not SPIR-V");
    }

    const std::string stem = source.stem().string();
    std::string artifact;
    if (stem == "testShader") {
        artifact = shader.stage == ShaderStage::Vertex ? "vert" : "frag";
    } else if (stem == "sky") {
        artifact = shader.stage == ShaderStage::Vertex ? "skyvert" : "skyfrag";
    } else if (stem == "compute") {
        artifact = "compute";
    } else if (stem == "gbuffer_cull") {
        artifact = "gbuffer_cull_comp";
    } else if (stem == "deferred_postprocess" &&
               shader.stage == ShaderStage::Fragment &&
               shader.hasPermutation) {
        artifact = stem + "_" + std::to_string(shader.permutation) + "_frag";
    } else {
        const char* suffix = shader.stage == ShaderStage::Vertex
            ? "_vert"
            : shader.stage == ShaderStage::Fragment ? "_frag" : "_comp";
        artifact = stem + suffix;
    }
    return (std::filesystem::path("res/Shaders/Bin") /
            (artifact + ".spv")).string();
}

} // namespace Tasrovy::RHI::Vulkan
