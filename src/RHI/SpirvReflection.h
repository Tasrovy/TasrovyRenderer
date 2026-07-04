#pragma once

#include "ReflectionData.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Tasrovy {

class SpirvReflection {
public:
    static ShaderReflectionData reflect(const std::string& spvPath, const std::string& entryPoint);
    static ShaderReflectionData reflectFromMemory(const uint32_t* data, size_t wordCount, const std::string& entryPoint);
    static std::vector<uint32_t> readSpvFile(const std::string& path);

private:
    static ReflectionMemberType mapType(uint32_t baseType, uint32_t vecsize, uint32_t columns);
};

} // namespace Tasrovy
