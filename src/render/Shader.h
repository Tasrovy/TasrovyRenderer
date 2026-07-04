#pragma once

#include <string>
#include <memory>

namespace Tasrovy {

class Shader : public std::enable_shared_from_this<Shader> {
public:
    static std::shared_ptr<Shader> create(const std::string& vertPath, const std::string& fragPath);

    void setVertexPath(const std::string& path);
    void setFragmentPath(const std::string& path);
    void setVertexEntry(const std::string& entry);
    void setFragmentEntry(const std::string& entry);

    const std::string& getVertexPath() const;
    const std::string& getFragmentPath() const;
    const std::string& getVertexEntry() const;
    const std::string& getFragmentEntry() const;

private:
    Shader() = default;
    Shader(const std::string& vertPath, const std::string& fragPath);

    std::string vertPath_;
    std::string fragPath_;
    std::string vertEntry_ = "VSMain";
    std::string fragEntry_ = "PSMain";
};

} // namespace Tasrovy
