#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <optional>

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
    static std::shared_ptr<Shader> create(
        const std::string& sourcePath,
        ShaderType type,
        std::optional<uint64_t> permutation = std::nullopt);

    void setSourcePath(const std::string& sourcePath);
    void setEntry(const std::string& entry);
    void setPermutation(std::optional<uint64_t> permutation);

    const std::string& getSourcePath() const;
    const std::string& getEntry() const;
    ShaderType getType() const;
    std::optional<uint64_t> getPermutation() const;

private:
    Shader() = default;
    Shader(
        const std::string& sourcePath,
        ShaderType type,
        std::optional<uint64_t> permutation);

    std::string sourcePath_;
    std::string entry_;
    ShaderType type_;
    std::optional<uint64_t> permutation_;
};

} // namespace Tasrovy::Render
