#include "DeferredPipeline.h"

#include "Material.h"
#include "Object.h"
#include "PipelinePass.h"
#include "Shader.h"
#include "Skybox.h"
#include <functional>
#include <unordered_set>

namespace Tasrovy::Render {

namespace {

std::shared_ptr<PipelinePass> createDeferredPass(
    const char* name,
    PipelinePassType type,
    PipelinePassExecution execution,
    CullMode cullMode,
    bool depthTest,
    bool depthWrite,
    DepthTestMode depthMode,
    BlendMode blendMode) {
    auto pass = PipelinePass::create(name);
    pass->setType(type);
    pass->setExecution(execution);
    pass->setTopology(Topology::TriangleList);
    pass->setCullMode(cullMode);
    pass->setDepthTest(depthTest);
    pass->setDepthWrite(depthWrite);
    pass->setDepthTestMode(depthMode);
    pass->setBlendMode(blendMode);
    return pass;
}

void addDeferredMaterialTextures(const std::shared_ptr<PipelinePass>& pass, bool includeOpacity) {
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
    if (includeOpacity) {
        pass->addMaterialTexture({
            MaterialTextureSemantic::Opacity,
            "opacityTexture",
            MaterialTextureColorSpace::Linear,
            MaterialTextureFallback::White
        });
    }
}

void addDeferredGBufferTextures(const std::shared_ptr<PipelinePass>& pass) {
    pass->addMaterialTexture({
        MaterialTextureSemantic::BaseColor,
        "baseColorTexture",
        MaterialTextureColorSpace::SRGB,
        MaterialTextureFallback::White
    });
}

} // namespace

std::shared_ptr<DeferredPipeline> DeferredPipeline::create(const std::string& name) {
    return std::shared_ptr<DeferredPipeline>(new DeferredPipeline(name));
}

DeferredPipeline::DeferredPipeline(const std::string& name)
    : PipelineBase(name) {
}

void DeferredPipeline::GenPass(std::shared_ptr<Scene> scene) {
    clearPasses();
    clearTextures();

    declareTexture({
        "ShadowMap", PipelineTextureFormat::Depth32Float,
        PipelineTextureExtent::Fixed, 1.0f, 1.0f, 2048, 2048
    });
    // Color textures are decoded from sRGB when sampled. Keep every
    // intermediate color target linear and use enough precision to avoid an
    // encode/decode or 8-bit quantization round-trip before presentation.
    declareTexture({"GBufferAlbedo", PipelineTextureFormat::RGBA16Float});
    declareTexture({"GBufferNormal", PipelineTextureFormat::RGBA16Float});
    declareTexture({"GBufferMaterial", PipelineTextureFormat::RGBA8Unorm});
    declareTexture({"GBufferWorldPos", PipelineTextureFormat::RGBA16Float});
    declareTexture({"SceneDepth", PipelineTextureFormat::Depth32Float});
    declareTexture({"SceneColor", PipelineTextureFormat::RGBA16Float});
    declareTexture({
        "FinalColor", PipelineTextureFormat::Swapchain,
        PipelineTextureExtent::RenderRelative, 1.0f, 1.0f, 0, 0, true
    });

    auto shadowPass = createDeferredPass(
        "Shadow", PipelinePassType::Shadow, PipelinePassExecution::Mesh,
        CullMode::Front, true, true, DepthTestMode::Less, BlendMode::Off);
    auto gBufferPass = createDeferredPass(
        "GBuffer", PipelinePassType::Geometry, PipelinePassExecution::Mesh,
        CullMode::None, true, true, DepthTestMode::Less, BlendMode::Off);
    auto lightingPass = createDeferredPass(
        "Lighting", PipelinePassType::Lighting, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto skyboxPass = createDeferredPass(
        "Skybox", PipelinePassType::Skybox, PipelinePassExecution::Skybox,
        CullMode::None, true, false, DepthTestMode::LessOrEqual, BlendMode::Off);
    auto transparentPass = createDeferredPass(
        "Transparent", PipelinePassType::Transparent, PipelinePassExecution::Mesh,
        CullMode::Back, true, false, DepthTestMode::Less, BlendMode::Alpha);
    auto postProcessPass = createDeferredPass(
        "PostProcessing", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);

    shadowPass->setDepthAttachment("ShadowMap");

    gBufferPass->addColorAttachment("GBufferAlbedo");
    gBufferPass->addColorAttachment("GBufferNormal");
    gBufferPass->addColorAttachment("GBufferMaterial");
    gBufferPass->addColorAttachment("GBufferWorldPos");
    gBufferPass->setDepthAttachment("SceneDepth");

    lightingPass->addSampledTexture("shadowMap", "ShadowMap", 1);
    lightingPass->addSampledTexture("gBufferAlbedo", "GBufferAlbedo", 2);
    lightingPass->addSampledTexture("gBufferNormal", "GBufferNormal", 3);
    lightingPass->addSampledTexture("gBufferMaterial", "GBufferMaterial", 4);
    lightingPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 6);
    lightingPass->addSampledTexture("sceneDepth", "SceneDepth", 7);
    lightingPass->addColorAttachment("SceneColor");

    skyboxPass->addColorAttachment("SceneColor", AttachmentLoad::Load);
    skyboxPass->setDepthAttachment(
        "SceneDepth", AttachmentLoad::Load, AttachmentStore::Store, true);

    transparentPass->addSampledTexture("shadowMap", "ShadowMap", 1);
    transparentPass->addColorAttachment("SceneColor", AttachmentLoad::Load);
    transparentPass->setDepthAttachment(
        "SceneDepth", AttachmentLoad::Load, AttachmentStore::Store, true);

    postProcessPass->addSampledTexture("sceneColor", "SceneColor", 1);
    postProcessPass->addColorAttachment("FinalColor");

    shadowPass->addMaterialTexture({
        MaterialTextureSemantic::Opacity,
        "opacityTexture",
        MaterialTextureColorSpace::Linear,
        MaterialTextureFallback::White
    });
    addDeferredGBufferTextures(gBufferPass);
    addDeferredMaterialTextures(transparentPass, true);

    shadowPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_shadow_vert.spv", ShaderType::Vertex));
    shadowPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_shadow_frag.spv", ShaderType::Fragment));
    gBufferPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_gbuffer_vert.spv", ShaderType::Vertex));
    gBufferPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_gbuffer_frag.spv", ShaderType::Fragment));
    lightingPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_lighting_vert.spv", ShaderType::Vertex));
    lightingPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_lighting_frag.spv", ShaderType::Fragment));
    skyboxPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\skyvert.spv", ShaderType::Vertex));
    skyboxPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\skyfrag.spv", ShaderType::Fragment));
    transparentPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_transparent_vert.spv", ShaderType::Vertex));
    transparentPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_transparent_frag.spv", ShaderType::Fragment));
    postProcessPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_postprocess_vert.spv", ShaderType::Vertex));
    postProcessPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_postprocess_frag.spv", ShaderType::Fragment));

    addPass(shadowPass);
    addPass(gBufferPass);
    addPass(lightingPass);
    addPass(skyboxPass);
    addPass(transparentPass);
    addPass(postProcessPass);

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
            const auto material = object->getMaterial();
            const bool transparent =
                material && material->getSurface() == MaterialSurface::Transparent;

            if (transparent) {
                transparentPass->addObject(object);
            } else {
                gBufferPass->addObject(object);
            }

            if (!material || material->castsShadows()) {
                shadowPass->addObject(object);
            }
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
