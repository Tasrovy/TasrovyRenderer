#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <variant>

#include "PipelineResource.h"
#include "Scene.h"

namespace Tasrovy::Render {

class PipelinePass;

struct PipelinePassDependency {
    std::string producerPass;
    std::string consumerPass;
    std::string resource;
};

using PipelineConfigurationValue =
    std::variant<bool, int64_t, double, std::string>;

struct PipelineConfiguration {
    std::unordered_map<std::string, PipelineConfigurationValue> values;

    template <typename T>
    T get(const std::string& key, T fallback) const {
        const auto found = values.find(key);
        if (found == values.end()) return fallback;
        if (const auto* value = std::get_if<T>(&found->second)) {
            return *value;
        }
        return fallback;
    }

    bool operator==(const PipelineConfiguration&) const = default;
};

namespace PipelineConfigKeys {
inline constexpr const char* ShadowTechnique = "render.shadow.technique";
inline constexpr const char* Hbao = "render.hbao.enabled";
inline constexpr const char* HiZ = "render.hiz.enabled";
inline constexpr const char* Ssr = "render.ssr.enabled";
inline constexpr const char* DepthOfField = "render.dof.enabled";
inline constexpr const char* TemporalMode = "render.temporal.mode";
inline constexpr const char* MotionBlur = "render.motion_blur.enabled";
inline constexpr const char* Outline = "render.outline.enabled";
inline constexpr const char* Bloom = "render.bloom.enabled";
}

class PipelineBase : public std::enable_shared_from_this<PipelineBase> {
public:
    virtual ~PipelineBase() = default;

    void setName(const std::string& name);
    const std::string& getName() const;

    virtual void GenPass(std::shared_ptr<Scene> scene) = 0;
    virtual bool applyConfiguration(
        const PipelineConfiguration& configuration);
    uint64_t getConfigurationVersion() const;

    std::shared_ptr<PipelinePass> getPass(const std::string& name) const;
    std::shared_ptr<PipelinePass> getPass(size_t index) const;
    size_t getPassCount() const;
    const std::vector<std::shared_ptr<PipelinePass>>& getPasses() const;
    bool hasPass(const std::string& name) const;

    const PipelineTextureDesc* getTexture(const std::string& name) const;
    const std::vector<PipelineTextureDesc>& getTextures() const;
    const PipelineBufferDesc* getBuffer(const std::string& name) const;
    const std::vector<PipelineBufferDesc>& getBuffers() const;
    std::vector<PipelinePassDependency> getPassDependencies() const;
    std::vector<std::string> validatePassDependencies() const;
    std::vector<std::string> validateResourceFlow() const;

protected:
    PipelineBase() = default;
    explicit PipelineBase(const std::string& name);

    // Declaration order is not execution order. RenderGraph resolves resource
    // producers and computes the final topological order.
    void addPass(std::shared_ptr<PipelinePass> pass);
    void clearPasses();
    void declareTexture(PipelineTextureDesc desc);
    void clearTextures();
    void declareBuffer(PipelineBufferDesc desc);
    void clearBuffers();
    void commitConfiguration(
        const PipelineConfiguration& configuration);
    void markConfigurationDirty();

    std::string name_;
    std::vector<std::shared_ptr<PipelinePass>> passes_;
    std::vector<PipelineTextureDesc> textures_;
    std::vector<PipelineBufferDesc> buffers_;
    PipelineConfiguration configuration_;
    uint64_t configurationVersion_ = 1;
};

} // namespace Tasrovy::Render
