#include "SpirvReflection.h"
#include <spirv_cross/spirv_cross.hpp>
#include <fstream>
#include <stdexcept>
#include <Logger.hpp>

namespace Tasrovy::RHI {

std::vector<uint32_t> SpirvReflection::readSpvFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIR-V file: " + path);
    }

    size_t fileSize = (size_t)file.tellg();
    if (fileSize % 4 != 0) {
        throw std::runtime_error("SPIR-V file size is not a multiple of 4: " + path);
    }

    std::vector<uint32_t> buffer(fileSize / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    return buffer;
}

ShaderReflectionData SpirvReflection::reflect(const std::string& spvPath, const std::string& entryPoint) {
    auto spirv = readSpvFile(spvPath);
    return reflectFromMemory(spirv.data(), spirv.size(), entryPoint);
}

ShaderReflectionData SpirvReflection::reflectFromMemory(const uint32_t* data, size_t wordCount, const std::string& entryPoint) {
    ShaderReflectionData result;

    spirv_cross::Compiler compiler(std::vector<uint32_t>(data, data + wordCount));

    auto resources = compiler.get_shader_resources();

    // Reflect uniform blocks (SPIR-V calls them uniform_buffers)
    for (auto& resource : resources.uniform_buffers) {
        ReflectedUniformBlock block;
        block.name = resource.name;
        block.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        block.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);

        auto& blockType = compiler.get_type(resource.base_type_id);

        // Get total block size from active buffer ranges
        auto ranges = compiler.get_active_buffer_ranges(resource.id);
        uint32_t maxEnd = 0;
        for (auto& range : ranges) {
            uint32_t end = range.offset + range.range;
            if (end > maxEnd) maxEnd = end;
        }
        block.blockSize = maxEnd;

        // Iterate members
        for (size_t i = 0; i < blockType.member_types.size(); ++i) {
            ReflectedMember member;
            member.name = compiler.get_member_name(blockType.self, i);
            member.offset = compiler.get_member_decoration(blockType.self, i, spv::DecorationOffset);

            auto& memberType = compiler.get_type(blockType.member_types[i]);
            member.type = mapType(memberType.basetype, memberType.vecsize, memberType.columns);

            // Calculate member size from active ranges
            for (auto& range : ranges) {
                if (range.offset == member.offset) {
                    member.size = range.range;
                    break;
                }
            }

            block.members.push_back(std::move(member));
        }

        result.totalUboSize += block.blockSize;
        result.uniformBlocks.push_back(std::move(block));
    }

    // Reflect sampled images (samplers/textures)
    for (auto& resource : resources.sampled_images) {
        ReflectedSamplerBinding sampler;
        sampler.name = resource.name;
        sampler.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        sampler.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        sampler.isTextureCube = false;
        result.samplers.push_back(std::move(sampler));
    }

    return result;
}

ReflectionMemberType SpirvReflection::mapType(uint32_t baseType, uint32_t vecsize, uint32_t columns) {
    if (columns > 1) {
        // Matrix
        if (columns == 2) return ReflectionMemberType::Mat2;
        if (columns == 3) return ReflectionMemberType::Mat3;
        return ReflectionMemberType::Mat4;
    }

    switch (baseType) {
        case spirv_cross::SPIRType::Float:
            switch (vecsize) {
                case 1: return ReflectionMemberType::Float;
                case 2: return ReflectionMemberType::Float2;
                case 3: return ReflectionMemberType::Float3;
                case 4: return ReflectionMemberType::Float4;
            }
            break;
        case spirv_cross::SPIRType::Int:
            switch (vecsize) {
                case 1: return ReflectionMemberType::Int;
                case 2: return ReflectionMemberType::Int2;
                case 3: return ReflectionMemberType::Int3;
                case 4: return ReflectionMemberType::Int4;
            }
            break;
    }

    return ReflectionMemberType::Float;
}

} // namespace Tasrovy::RHI
