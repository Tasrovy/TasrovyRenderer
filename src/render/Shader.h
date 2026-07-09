#pragma once

#include <string>
#include <memory>

namespace Tasrovy::Render {

enum class ShaderType {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    Tessellation
};

class Shader : public std::enable_shared_from_this<Shader> {
public:
    static std::shared_ptr<Shader> create(const std::string& path, ShaderType type);

    void setPath(const std::string& path);
    void setEntry(const std::string& entry);

    const std::string& getPath() const;
    const std::string& getEntry() const;
    ShaderType getType() const;

private:
    Shader() = default;
    Shader(const std::string& path, ShaderType type);

    std::string path_;
    std::string entry_;
    ShaderType type_;
};

} // namespace Tasrovy::Render
