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
    declareTexture({"GBufferEffects", PipelineTextureFormat::RGBA16Float});
    declareTexture({"SceneDepth", PipelineTextureFormat::Depth32Float});
    declareTexture({
        "HBAO", PipelineTextureFormat::RG16Float,
        PipelineTextureExtent::RenderRelative, 0.5f, 0.5f
    });
    declareTexture({
        "HiZHalf", PipelineTextureFormat::RG16Float,
        PipelineTextureExtent::RenderRelative, 0.5f, 0.5f
    });
    declareTexture({
        "HiZQuarter", PipelineTextureFormat::RG16Float,
        PipelineTextureExtent::RenderRelative, 0.25f, 0.25f
    });
    declareTexture({
        "HiZEighth", PipelineTextureFormat::RG16Float,
        PipelineTextureExtent::RenderRelative, 0.125f, 0.125f
    });
    declareTexture({
        "HiZSixteenth", PipelineTextureFormat::RG16Float,
        PipelineTextureExtent::RenderRelative, 0.0625f, 0.0625f
    });
    declareTexture({"SceneColor", PipelineTextureFormat::RGBA16Float});
    declareTexture({
        "BloomLowRes", PipelineTextureFormat::RGBA16Float,
        PipelineTextureExtent::RenderRelative, 0.25f, 0.25f
    });
    declareTexture({
        "FinalColor", PipelineTextureFormat::Swapchain,
        PipelineTextureExtent::RenderRelative, 1.0f, 1.0f, 0, 0, true
    });

    auto shadowPass = createDeferredPass(
        "Shadow", PipelinePassType::Shadow, PipelinePassExecution::Mesh,
        // Use the same winding convention as visible geometry. Shader-side
        // slope/minimum bias handles self-shadowing without dropping thin meshes.
        CullMode::Back, true, true, DepthTestMode::Less, BlendMode::Off);
    auto gBufferPass = createDeferredPass(
        "GBuffer", PipelinePassType::Geometry, PipelinePassExecution::Mesh,
        CullMode::Back, true, true, DepthTestMode::Less, BlendMode::Off);
    auto lightingPass = createDeferredPass(
        "Lighting", PipelinePassType::Lighting, PipelinePassExecution::Fullscreen,
        // Fullscreen triangles must not depend on model winding.
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto hbaoPass = createDeferredPass(
        "HBAO", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto hiZInitPass = createDeferredPass(
        "HiZHalf", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto hiZQuarterPass = createDeferredPass(
        "HiZQuarter", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto hiZEighthPass = createDeferredPass(
        "HiZEighth", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto hiZSixteenthPass = createDeferredPass(
        "HiZSixteenth", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto skyboxPass = createDeferredPass(
        "Skybox", PipelinePassType::Skybox, PipelinePassExecution::Skybox,
        // The skybox cube is viewed from inside; rendering both sides keeps
        // this independent of the source cube's winding convention.
        CullMode::None, true, false, DepthTestMode::LessOrEqual, BlendMode::Off);
    auto transparentPass = createDeferredPass(
        "Transparent", PipelinePassType::Transparent, PipelinePassExecution::Mesh,
        CullMode::Back, true, false, DepthTestMode::Less, BlendMode::Alpha);
    auto bloomPass = createDeferredPass(
        "BloomLowRes", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto postProcessPass = createDeferredPass(
        "PostProcessing", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        // Fullscreen triangles must not depend on model winding.
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);

    shadowPass->setDepthAttachment("ShadowMap");

    gBufferPass->addColorAttachment("GBufferAlbedo");
    gBufferPass->addColorAttachment("GBufferNormal");
    gBufferPass->addColorAttachment("GBufferMaterial");
    gBufferPass->addColorAttachment("GBufferWorldPos");
    gBufferPass->addColorAttachment("GBufferEffects");
    gBufferPass->setDepthAttachment("SceneDepth");

    hbaoPass->addSampledTexture("sceneDepth", "SceneDepth", 1);
    hbaoPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 2);
    hbaoPass->addSampledTexture("gBufferNormal", "GBufferNormal", 3);
    hbaoPass->addColorAttachment("HBAO");

    hiZInitPass->addSampledTexture("sceneDepth", "SceneDepth", 1);
    hiZInitPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 2);
    hiZInitPass->addColorAttachment("HiZHalf");
    hiZQuarterPass->addSampledTexture("hiZInput", "HiZHalf", 1);
    hiZQuarterPass->addColorAttachment("HiZQuarter");
    hiZEighthPass->addSampledTexture("hiZInput", "HiZQuarter", 1);
    hiZEighthPass->addColorAttachment("HiZEighth");
    hiZSixteenthPass->addSampledTexture("hiZInput", "HiZEighth", 1);
    hiZSixteenthPass->addColorAttachment("HiZSixteenth");

    lightingPass->addSampledTexture("shadowMap", "ShadowMap", 1);
    lightingPass->addSampledTexture("gBufferAlbedo", "GBufferAlbedo", 2);
    lightingPass->addSampledTexture("gBufferNormal", "GBufferNormal", 3);
    lightingPass->addSampledTexture("gBufferMaterial", "GBufferMaterial", 4);
    lightingPass->addSampledTexture("gBufferEffects", "GBufferEffects", 5);
    lightingPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 6);
    lightingPass->addSampledTexture("sceneDepth", "SceneDepth", 7);
    lightingPass->addSampledTexture("hbaoTexture", "HBAO", 11);
    lightingPass->addColorAttachment("SceneColor");

    skyboxPass->addColorAttachment("SceneColor", AttachmentLoad::Load);
    skyboxPass->setDepthAttachment(
        "SceneDepth", AttachmentLoad::Load, AttachmentStore::Store, true);

    transparentPass->addSampledTexture("shadowMap", "ShadowMap", 1);
    transparentPass->addColorAttachment("SceneColor", AttachmentLoad::Load);
    transparentPass->setDepthAttachment(
        "SceneDepth", AttachmentLoad::Load, AttachmentStore::Store, true);

    bloomPass->addSampledTexture("sceneColor", "SceneColor", 1);
    bloomPass->addColorAttachment("BloomLowRes");

    // One uber postprocess shader performs SSR, bloom, outline, tone mapping
    // and final composition in a single fullscreen draw. Hi-Z generation stays
    // separate because each level depends on the minimum depth of the prior one.
    postProcessPass->addSampledTexture("sceneColor", "SceneColor", 1);
    postProcessPass->addSampledTexture("gBufferNormal", "GBufferNormal", 2);
    postProcessPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 3);
    postProcessPass->addSampledTexture("sceneDepth", "SceneDepth", 4);
    postProcessPass->addSampledTexture("gBufferMaterial", "GBufferMaterial", 5);
    postProcessPass->addSampledTexture("hiZHalf", "HiZHalf", 6);
    postProcessPass->addSampledTexture("hiZQuarter", "HiZQuarter", 7);
    postProcessPass->addSampledTexture("hiZEighth", "HiZEighth", 8);
    postProcessPass->addSampledTexture("hiZSixteenth", "HiZSixteenth", 9);
    // The renderer binds these two slots to the immediately preceding
    // frame-in-flight resources rather than the current frame's attachments.
    postProcessPass->addSampledTexture("taaHistoryColor", "SceneColor", 10);
    postProcessPass->addSampledTexture("taaHistoryDepth", "SceneDepth", 11);
    // Opaque albedo/normal alpha channels carry the two signed velocity
    // components, so TAA needs no sixth MRT or extra geometry pass.
    postProcessPass->addSampledTexture("gBufferAlbedo", "GBufferAlbedo", 12);
    postProcessPass->addSampledTexture("bloomLowRes", "BloomLowRes", 13);
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
    hbaoPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_hbao_vert.spv", ShaderType::Vertex));
    hbaoPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_hbao_frag.spv", ShaderType::Fragment));
    hiZInitPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_init_vert.spv", ShaderType::Vertex));
    hiZInitPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_init_frag.spv", ShaderType::Fragment));
    hiZQuarterPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_reduce_vert.spv", ShaderType::Vertex));
    hiZQuarterPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_reduce_frag.spv", ShaderType::Fragment));
    hiZEighthPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_reduce_vert.spv", ShaderType::Vertex));
    hiZEighthPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_reduce_frag.spv", ShaderType::Fragment));
    hiZSixteenthPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_reduce_vert.spv", ShaderType::Vertex));
    hiZSixteenthPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_hiz_reduce_frag.spv", ShaderType::Fragment));
    lightingPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_lighting_vert.spv", ShaderType::Vertex));
    lightingPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_lighting_frag.spv", ShaderType::Fragment));
    skyboxPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\skyvert.spv", ShaderType::Vertex));
    skyboxPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\skyfrag.spv", ShaderType::Fragment));
    transparentPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_transparent_vert.spv", ShaderType::Vertex));
    transparentPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_transparent_frag.spv", ShaderType::Fragment));
    bloomPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_bloom_lowres_vert.spv", ShaderType::Vertex));
    bloomPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_bloom_lowres_frag.spv", ShaderType::Fragment));
    postProcessPass->setVertexShader(Shader::create("res\\Shaders\\Bin\\deferred_postprocess_vert.spv", ShaderType::Vertex));
    postProcessPass->setFragmentShader(Shader::create("res\\Shaders\\Bin\\deferred_postprocess_frag.spv", ShaderType::Fragment));

    addPass(shadowPass);
    addPass(gBufferPass);
    addPass(hbaoPass);
    addPass(hiZInitPass);
    addPass(hiZQuarterPass);
    addPass(hiZEighthPass);
    addPass(hiZSixteenthPass);
    addPass(lightingPass);
    addPass(skyboxPass);
    addPass(transparentPass);
    addPass(bloomPass);
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
