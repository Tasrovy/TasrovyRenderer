#include "Shader.h"

namespace Tasrovy {

std::shared_ptr<Shader> Shader::create(const std::string& vertPath, const std::string& fragPath) {
    return std::shared_ptr<Shader>(new Shader(vertPath, fragPath));
}

Shader::Shader(const std::string& vertPath, const std::string& fragPath)
    : vertPath_(vertPath)
    , fragPath_(fragPath) {
}

void Shader::setVertexPath(const std::string& path) { vertPath_ = path; }
void Shader::setFragmentPath(const std::string& path) { fragPath_ = path; }
void Shader::setVertexEntry(const std::string& entry) { vertEntry_ = entry; }
void Shader::setFragmentEntry(const std::string& entry) { fragEntry_ = entry; }

const std::string& Shader::getVertexPath() const { return vertPath_; }
const std::string& Shader::getFragmentPath() const { return fragPath_; }
const std::string& Shader::getVertexEntry() const { return vertEntry_; }
const std::string& Shader::getFragmentEntry() const { return fragEntry_; }

} // namespace Tasrovy
