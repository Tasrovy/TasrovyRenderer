#pragma once

#include "../Device.h"

#include <string>

namespace Tasrovy::RHI::Vulkan {

// Resolves an API-independent HLSL shader description to the Vulkan artifact
// produced by res/Scripts/compile.bat. Render code never refers to SPIR-V.
std::string resolveShaderBinary(const ShaderModuleDesc& shader);

} // namespace Tasrovy::RHI::Vulkan
