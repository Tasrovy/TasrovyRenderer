#include "PBRPipeline.h"

#include "Material.h"
#include "Object.h"
#include "PipelinePass.h"
#include "Shader.h"
#include "Skybox.h"
#include <functional>
#include <unordered_set>

namespace Tasrovy::Render {

namespace {

std::shared_ptr<PipelinePass> createPass(
    const char* name,
    PipelinePassType type,
    CullMode cullMode,
    bool depthTest,
    bool depthWrite,
    DepthTestMode depthMode,
    BlendMode blendMode) {
    auto pass = PipelinePass::create(name);
    pass->setType(type);
    pass->setTopology(Topology::TriangleList);
    pass->setCullMode(cullMode);
    pass->setDepthTest(depthTest);
    pass->setDepthWrite(depthWrite);
    pass->setDepthTestMode(depthMode);
    pass->setBlendMode(blendMode);
    return pass;
}

void addPBRMaterialTextures(const std::shared_ptr<PipelinePass>& pass) {
    pass->addMaterialTexture({
        MaterialTextureSemantic::BaseColor,
        "baseColorTexture",
        MaterialTextureColorSpace::SRGB,
        MaterialTextureFallback::White
    });
    pass->addMaterialTexture({
        MaterialTextureSemantic::Normal,
        "normalTexture",
        MaterialTextureColorSpace::Linear,
        MaterialTextureFallback::FlatNormal
    });
    pass->addMaterialTexture({
        MaterialTextureSemantic::MetallicRoughnessAO,
        "metallicRoughnessAOTexture",
        MaterialTextureColorSpace::Linear,
        MaterialTextureFallback::MetallicRoughnessAO
    });
    pass->addMaterialTexture({
        MaterialTextureSemantic::Emissive,
        "emissiveTexture",
        MaterialTextureColorSpace::SRGB,
        MaterialTextureFallback::Black
    });
    pass->addMaterialTexture({
        MaterialTextureSemantic::Opacity,
        "opacityTexture",
        MaterialTextureColorSpace::Linear,
        MaterialTextureFallback::White
    });
}

} // namespace

std::shared_ptr<PBRPipeline> PBRPipeline::create(const std::string& name) {
    return std::shared_ptr<PBRPipeline>(new PBRPipeline(name));
}

PBRPipeline::PBRPipeline(const std::string& name)
    : PipelineBase(name) {
}

void PBRPipeline::GenPass(std::shared_ptr<Scene> scene) {
    clearPasses();
    clearTextures();

    declareTexture({
        "FinalColor", PipelineTextureFormat::Swapchain,
        PipelineTextureExtent::RenderRelative, 1.0f, 1.0f, 0, 0, true
    });

    auto skyboxPass = createPass(
        "Skybox", PipelinePassType::Skybox, CullMode::None,
        true, false, DepthTestMode::LessOrEqual, BlendMode::Off);
    auto forwardPass = createPass(
        "Forward", PipelinePassType::Generic, CullMode::Back,
        true, true, DepthTestMode::Less, BlendMode::Off);

    skyboxPass->setExecution(PipelinePassExecution::Skybox);
    skyboxPass->addColorAttachment("FinalColor");
    forwardPass->addColorAttachment("FinalColor", AttachmentLoad::Load);
    addPBRMaterialTextures(forwardPass);

    skyboxPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\skyvert.spv", ShaderType::Vertex));
    skyboxPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\skyfrag.spv", ShaderType::Fragment));
    forwardPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\vert.spv", ShaderType::Vertex));
    forwardPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\frag.spv", ShaderType::Fragment));

    addPass(skyboxPass);
    addPass(forwardPass);

    if (!scene) {
        return;
    }

    std::unordered_set<const Object*> visited;
    std::function<void(const std::shared_ptr<Object>&)> collectObject;
    collectObject = [&](const std::shared_ptr<Object>& object) {
        if (!object || !object->isActive() || !visited.insert(object.get()).second) {
            return;
        }

        if (std::dynamic_pointer_cast<Skybox>(object)) {
            skyboxPass->addObject(object);
        } else if (object->getMesh()) {
            forwardPass->addObject(object);
        }

        for (const auto& child : object->getChildren()) {
            collectObject(child);
        }
    };

    for (const auto& object : scene->getObjects()) {
        collectObject(object);
    }
}

} // namespace Tasrovy::Render
