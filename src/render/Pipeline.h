#pragma once

#include <string>
#include <vector>
#include <memory>

namespace Tasrovy {

class PipelinePass;

class Pipeline : public std::enable_shared_from_this<Pipeline> {
public:
    static std::shared_ptr<Pipeline> create(const std::string& name = "");

    void setName(const std::string& name);
    const std::string& getName() const;

    void addPass(std::shared_ptr<PipelinePass> pass);
    void insertPass(size_t index, std::shared_ptr<PipelinePass> pass);
    void removePass(const std::string& passName);
    void removePass(size_t index);
    void clearPasses();

    void movePass(size_t fromIndex, size_t toIndex);

    std::shared_ptr<PipelinePass> getPass(const std::string& name) const;
    std::shared_ptr<PipelinePass> getPass(size_t index) const;
    size_t getPassCount() const;
    const std::vector<std::shared_ptr<PipelinePass>>& getPasses() const;

    bool hasPass(const std::string& name) const;

private:
    Pipeline() = default;
    explicit Pipeline(const std::string& name);

    std::string name_;
    std::vector<std::shared_ptr<PipelinePass>> passes_;
};

} // namespace Tasrovy
