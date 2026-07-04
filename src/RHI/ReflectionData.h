#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Tasrovy {

class Material;

enum class ReflectionMemberType : uint32_t {
    Float, Float2, Float3, Float4,
    Int, Int2, Int3, Int4,
    Mat2, Mat3, Mat4
};

struct ReflectedMember {
    std::string name;
    ReflectionMemberType type = ReflectionMemberType::Float;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct ReflectedUniformBlock {
    std::string name;
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t blockSize = 0;
    std::vector<ReflectedMember> members;
};

struct ReflectedSamplerBinding {
    std::string name;
    uint32_t binding = 0;
    uint32_t set = 0;
    bool isTextureCube = false;
};

struct ShaderReflectionData {
    std::vector<ReflectedUniformBlock> uniformBlocks;
    std::vector<ReflectedSamplerBinding> samplers;
    uint32_t totalUboSize = 0;
};

struct ReflectionRequest {
    Material* material = nullptr;
    std::string vertSpvPath;
    std::string fragSpvPath;
    std::string vertEntryPoint = "VSMain";
    std::string fragEntryPoint = "PSMain";
};

struct ReflectionResult {
    Material* material = nullptr;
    ShaderReflectionData vertexData;
    ShaderReflectionData fragmentData;
    bool success = true;
    std::string errorMessage;
};

} // namespace Tasrovy
