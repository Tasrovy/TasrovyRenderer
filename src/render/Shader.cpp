#include "Shader.h"

namespace Tasrovy::Render {

std::shared_ptr<Shader> Shader::create(
    const std::string& sourcePath,
    ShaderType type,
    std::optional<uint64_t> permutation) {
    return std::shared_ptr<Shader>(
        new Shader(sourcePath, type, permutation));
}

Shader::Shader(
    const std::string& sourcePath,
    ShaderType type,
    std::optional<uint64_t> permutation)
    : sourcePath_(sourcePath), type_(type), permutation_(permutation) {
    switch (type_) {
    case ShaderType::Vertex:
        entry_ = "VSMain";
        break;
    case ShaderType::Fragment:
        entry_ = "PSMain";
        break;
    case ShaderType::Compute:
        entry_ = "CSMain";
        break;
    default:
        entry_ = "main";
        break;
    }
}

void Shader::setSourcePath(const std::string& sourcePath) { sourcePath_ = sourcePath; }
void Shader::setEntry(const std::string& entry) { entry_ = entry; }
void Shader::setPermutation(std::optional<uint64_t> permutation) {
    permutation_ = permutation;
}

const std::string& Shader::getSourcePath() const { return sourcePath_; }
const std::string& Shader::getEntry() const { return entry_; }
ShaderType Shader::getType() const { return type_; }
std::optional<uint64_t> Shader::getPermutation() const { return permutation_; }

} // namespace Tasrovy::Render
