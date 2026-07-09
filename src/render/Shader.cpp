#include "Shader.h"

namespace Tasrovy::Render {

std::shared_ptr<Shader> Shader::create(const std::string& path, ShaderType type) {
    return std::shared_ptr<Shader>(new Shader(path, type));
}

Shader::Shader(const std::string& path, ShaderType type)
    : path_(path), type_(type) {
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

void Shader::setPath(const std::string& path) { path_ = path; }
void Shader::setEntry(const std::string& entry) { entry_ = entry; }

const std::string& Shader::getPath() const { return path_; }
const std::string& Shader::getEntry() const { return entry_; }
ShaderType Shader::getType() const { return type_; }

} // namespace Tasrovy::Render
