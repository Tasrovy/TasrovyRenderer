#include "DeferredPipeline.h"

#include "Material.h"
#include "Mesh.h"
#include "Object.h"
#include "PBRMaterialBindings.h"
#include "PipelinePass.h"
#include "Shader.h"
#include "Skybox.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>

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
    if (execution == PipelinePassExecution::Fullscreen) {
        pass->setVertexLayout({});
        pass->setUniformByteSize(
            1600u,
            PipelineShaderStageVertex | PipelineShaderStageFragment);
    } else if (execution == PipelinePassExecution::Skybox) {
        pass->setVertexLayout({
            sizeof(float) * 3u,
            {{0, PipelineVertexFormat::Float3, 0}}
        });
        pass->setUniformByteSize(
            128u,
            PipelineShaderStageVertex | PipelineShaderStageFragment);
    } else if (execution == PipelinePassExecution::Mesh &&
               type == PipelinePassType::Shadow) {
        pass->setVertexLayout({
            sizeof(MeshVertex),
            {{MeshVertexLocation::Position,
              PipelineVertexFormat::Float3,
              static_cast<uint32_t>(offsetof(MeshVertex, position))}}
        });
        pass->setUniformByteSize(
            1600u,
            PipelineShaderStageVertex | PipelineShaderStageFragment);
    } else if (execution == PipelinePassExecution::Mesh) {
        pass->setVertexLayout({
            sizeof(MeshVertex),
            {
                {MeshVertexLocation::Position, PipelineVertexFormat::Float3,
                    static_cast<uint32_t>(offsetof(MeshVertex, position))},
                {MeshVertexLocation::Normal, PipelineVertexFormat::Float3,
                    static_cast<uint32_t>(offsetof(MeshVertex, normal))},
                {MeshVertexLocation::Tangent, PipelineVertexFormat::Float3,
                    static_cast<uint32_t>(offsetof(MeshVertex, tangent))},
                {MeshVertexLocation::Bitangent, PipelineVertexFormat::Float3,
                    static_cast<uint32_t>(offsetof(MeshVertex, bitangent))},
                {MeshVertexLocation::UV0, PipelineVertexFormat::Float2,
                    static_cast<uint32_t>(offsetof(MeshVertex, uv0))}
            }
        });
        pass->setUniformByteSize(
            1600u,
            PipelineShaderStageVertex | PipelineShaderStageFragment);
    }
    return pass;
}

void addDeferredMaterialTextures(const std::shared_ptr<PipelinePass>& pass) {
    for (const auto& binding : getPBRMaterialTextureBindings()) {
        pass->addMaterialTexture(binding);
    }
}

void addDeferredGBufferTextures(const std::shared_ptr<PipelinePass>& pass) {
    pass->addMaterialTexture(
        requirePBRMaterialTextureBinding("baseColorTexture"));
}

} // namespace

std::shared_ptr<DeferredPipeline> DeferredPipeline::create(const std::string& name) {
    return std::shared_ptr<DeferredPipeline>(new DeferredPipeline(name));
}

DeferredPipeline::DeferredPipeline(const std::string& name)
    : PipelineBase(name) {
}

void DeferredPipeline::setConfig(const DeferredPipelineConfig& config) {
    if (config_ == config) return;
    config_ = config;
    markConfigurationDirty();
}

const DeferredPipelineConfig& DeferredPipeline::getConfig() const {
    return config_;
}

bool DeferredPipeline::applyConfiguration(
    const PipelineConfiguration& configuration) {
    DeferredPipelineConfig next = config_;
    const auto shadow = configuration.get<int64_t>(
        PipelineConfigKeys::ShadowTechnique,
        static_cast<int64_t>(next.shadowTechnique));
    next.shadowTechnique = static_cast<DeferredShadowTechnique>(
        std::clamp<int64_t>(shadow, 0, 2));
    next.hbao = configuration.get<bool>(
        PipelineConfigKeys::Hbao, next.hbao);
    next.hiZ = configuration.get<bool>(
        PipelineConfigKeys::HiZ, next.hiZ);
    next.ssr = configuration.get<bool>(
        PipelineConfigKeys::Ssr, next.ssr);
    next.depthOfField = configuration.get<bool>(
        PipelineConfigKeys::DepthOfField, next.depthOfField);
    next.temporalMode = static_cast<uint8_t>(std::clamp<int64_t>(
        configuration.get<int64_t>(
            PipelineConfigKeys::TemporalMode, next.temporalMode),
        0,
        2));
    next.motionBlur = configuration.get<bool>(
        PipelineConfigKeys::MotionBlur, next.motionBlur);
    next.outline = configuration.get<bool>(
        PipelineConfigKeys::Outline, next.outline);
    next.bloom = configuration.get<bool>(
        PipelineConfigKeys::Bloom, next.bloom);
    if (next == config_) return false;
    config_ = next;
    commitConfiguration(configuration);
    return true;
}

void DeferredPipeline::GenPass(std::shared_ptr<Scene> scene) {
    clearPasses();
    clearTextures();
    clearBuffers();

    constexpr uint32_t ShadowCascadeCount = 4;
    constexpr uint32_t VirtualShadowPageSize = 2048;
    constexpr uint32_t VirtualShadowAtlasSize = 4096;
    if (config_.shadowTechnique == DeferredShadowTechnique::VirtualShadowMap) {
        declareTexture({
            "VirtualShadowAtlas",
            PipelineTextureFormat::Depth32Float,
            PipelineTextureExtent::Fixed,
            1.0f,
            1.0f,
            VirtualShadowAtlasSize,
            VirtualShadowAtlasSize
        });
    } else {
        const uint32_t shadowTextureCount =
            config_.shadowTechnique == DeferredShadowTechnique::ShadowMap
                ? 1u
                : ShadowCascadeCount;
        for (uint32_t cascade = 0; cascade < shadowTextureCount; ++cascade) {
            declareTexture({
                "ShadowMap" + std::to_string(cascade),
                PipelineTextureFormat::Depth32Float,
                PipelineTextureExtent::Fixed, 1.0f, 1.0f, 2048, 2048
            });
        }
    }
    // Color textures are decoded from sRGB when sampled. Keep every
    // intermediate color target linear and use enough precision to avoid an
    // encode/decode or 8-bit quantization round-trip before presentation.
    declareTexture({"GBufferAlbedo", PipelineTextureFormat::RGBA16Float});
    declareTexture({"GBufferNormal", PipelineTextureFormat::RGBA16Float});
    declareTexture({"GBufferVelocity", PipelineTextureFormat::RG16Float});
    declareTexture({"GBufferMaterial", PipelineTextureFormat::RGBA8Unorm});
    declareTexture({"GBufferWorldPos", PipelineTextureFormat::RGBA16Float});
    declareTexture({"GBufferEffects", PipelineTextureFormat::RGBA16Float});
    declareTexture({"SceneDepth", PipelineTextureFormat::Depth32Float});
    if (config_.hbao) {
        declareTexture({
            "HBAO", PipelineTextureFormat::RG16Float,
            PipelineTextureExtent::InternalRelative, 0.5f, 0.5f
        });
    }
    if (config_.hiZ) {
        declareTexture({
            "HiZHalf", PipelineTextureFormat::RG16Float,
            PipelineTextureExtent::InternalRelative, 0.5f, 0.5f
        });
        declareTexture({
            "HiZQuarter", PipelineTextureFormat::RG16Float,
            PipelineTextureExtent::InternalRelative, 0.25f, 0.25f
        });
        declareTexture({
            "HiZEighth", PipelineTextureFormat::RG16Float,
            PipelineTextureExtent::InternalRelative, 0.125f, 0.125f
        });
        declareTexture({
            "HiZSixteenth", PipelineTextureFormat::RG16Float,
            PipelineTextureExtent::InternalRelative, 0.0625f, 0.0625f
        });
    }
    declareTexture({"SceneColor", PipelineTextureFormat::RGBA16Float});
    if (config_.ssr) {
        declareTexture({"SceneColorSSR", PipelineTextureFormat::RGBA16Float});
    }
    if (config_.depthOfField) {
        declareTexture({"SceneColorDOF", PipelineTextureFormat::RGBA16Float});
    }
    if (config_.bloom) {
        declareTexture({
            "BloomDownHalf", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 0.5f, 0.5f
        });
        declareTexture({
            "BloomDownQuarter", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 0.25f, 0.25f
        });
        declareTexture({
            "BloomDownEighth", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 0.125f, 0.125f
        });
        declareTexture({
            "BloomDownSixteenth", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 0.0625f, 0.0625f
        });
        declareTexture({
            "BloomUpEighth", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 0.125f, 0.125f
        });
        declareTexture({
            "BloomUpQuarter", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 0.25f, 0.25f
        });
        declareTexture({
            "BloomLowRes", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 0.5f, 0.5f
        });
    }
    if (config_.temporalMode == 1) {
        declareTexture({
            "TAAColor", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::InternalRelative, 1.0f, 1.0f
        });
    } else if (config_.temporalMode == 2) {
        declareTexture({
            "TemporalColor", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 1.0f, 1.0f
        });
    }
    if (config_.temporalMode != 0) {
        declareTexture({
            "TemporalHistoryData", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 1.0f, 1.0f
        });
    }
    if (config_.motionBlur) {
        declareTexture({
            "MotionBlurColor", PipelineTextureFormat::RGBA16Float,
            PipelineTextureExtent::DisplayRelative, 1.0f, 1.0f
        });
    }
    if (config_.outline) {
        declareTexture({
            "OutlineHistory", PipelineTextureFormat::RG16Float,
            PipelineTextureExtent::DisplayRelative, 1.0f, 1.0f
        });
    }
    declareTexture({
        "FinalColor", PipelineTextureFormat::Swapchain,
        PipelineTextureExtent::DisplayRelative, 1.0f, 1.0f, 0, 0, true
    });

    std::array<std::shared_ptr<PipelinePass>, ShadowCascadeCount> shadowPasses;
    std::array<std::shared_ptr<PipelinePass>, ShadowCascadeCount>
        virtualShadowPasses;
    for (uint32_t cascade = 0; cascade < ShadowCascadeCount; ++cascade) {
        shadowPasses[cascade] = createDeferredPass(
            ("ShadowCascade" + std::to_string(cascade)).c_str(),
            PipelinePassType::Shadow, PipelinePassExecution::Mesh,
            // Use the same winding convention as visible geometry. Shader-side
            // slope/minimum bias handles self-shadowing without dropping thin meshes.
            CullMode::Back, true, true, DepthTestMode::Less, BlendMode::Off);
        shadowPasses[cascade]->setParameterProvider(
            ParameterProviders::Shadow);
        shadowPasses[cascade]->setUniformByteSize(
            192u, PipelineShaderStageVertex);
        shadowPasses[cascade]->setViewIndex(cascade);
        virtualShadowPasses[cascade] = createDeferredPass(
            ("VirtualShadowPage" + std::to_string(cascade)).c_str(),
            PipelinePassType::Shadow, PipelinePassExecution::Mesh,
            CullMode::Back, true, true, DepthTestMode::Less, BlendMode::Off);
        virtualShadowPasses[cascade]->setParameterProvider(
            ParameterProviders::Shadow);
        virtualShadowPasses[cascade]->setUniformByteSize(
            192u, PipelineShaderStageVertex);
        virtualShadowPasses[cascade]->setViewIndex(cascade);
        virtualShadowPasses[cascade]->setVirtualShadowPage({
            cascade & 1u,
            cascade >> 1u,
            VirtualShadowPageSize,
            VirtualShadowAtlasSize,
            cascade
        });
    }
    auto gBufferPass = createDeferredPass(
        "GBuffer", PipelinePassType::Geometry, PipelinePassExecution::Mesh,
        CullMode::Back, true, true, DepthTestMode::Less, BlendMode::Off);
    gBufferPass->setUniformByteSize(0u, 0u);
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
    auto transparentPass = createDeferredPass(
        "Transparent", PipelinePassType::Transparent, PipelinePassExecution::Mesh,
        CullMode::Back, true, false, DepthTestMode::Less, BlendMode::Alpha);
    transparentPass->setUniformByteSize(0u, 0u);
    auto bloomDownHalfPass = createDeferredPass(
        "BloomDownHalf", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto bloomDownQuarterPass = createDeferredPass(
        "BloomDownQuarter", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto bloomDownEighthPass = createDeferredPass(
        "BloomDownEighth", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto bloomDownSixteenthPass = createDeferredPass(
        "BloomDownSixteenth", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto bloomUpEighthPass = createDeferredPass(
        "BloomUpEighth", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto bloomUpQuarterPass = createDeferredPass(
        "BloomUpQuarter", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto bloomUpHalfPass = createDeferredPass(
        "BloomUpHalf", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto ssrPass = createDeferredPass(
        "SSR", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto depthOfFieldPass = createDeferredPass(
        "DepthOfField", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto temporalAaPass = createDeferredPass(
        "TemporalAA", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto temporalUpscalePass = createDeferredPass(
        "TemporalUpscale", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto motionBlurPass = createDeferredPass(
        "MotionBlur", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto outlineTemporalPass = createDeferredPass(
        "OutlineTemporal", PipelinePassType::PostProcess,
        PipelinePassExecution::Fullscreen,
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);
    auto postProcessPass = createDeferredPass(
        "PostProcessing", PipelinePassType::PostProcess, PipelinePassExecution::Fullscreen,
        // Fullscreen triangles must not depend on model winding.
        CullMode::None, false, false, DepthTestMode::Less, BlendMode::Off);

    lightingPass->setParameterProvider(ParameterProviders::Lighting);
    lightingPass->setUniformByteSize(
        480u, PipelineShaderStageFragment);
    hbaoPass->setParameterProvider(ParameterProviders::SSAO);
    hbaoPass->setUniformByteSize(
        16u, PipelineShaderStageFragment);
    lightingPass->addImportedTexture({
        8, ImportedResourceHandles::IblIrradiance,
        PipelineShaderStageFragment
    });
    lightingPass->addImportedTexture({
        9, ImportedResourceHandles::IblPrefiltered,
        PipelineShaderStageFragment
    });
    lightingPass->addImportedTexture({
        10, ImportedResourceHandles::IblBrdfLut,
        PipelineShaderStageFragment
    });
    bloomDownHalfPass->setParameterProvider(
        ParameterProviders::BloomPrefilter);
    for (const auto& bloomPass : {
             bloomDownHalfPass, bloomDownQuarterPass,
             bloomDownEighthPass, bloomDownSixteenthPass,
             bloomUpEighthPass, bloomUpQuarterPass, bloomUpHalfPass}) {
        bloomPass->setParameterProvider(ParameterProviders::BloomPrefilter);
        bloomPass->setUniformByteSize(
            16u, PipelineShaderStageFragment);
    }
    depthOfFieldPass->setParameterProvider(
        ParameterProviders::DepthOfField);
    temporalAaPass->setParameterProvider(
        ParameterProviders::TemporalAA);
    temporalAaPass->setUniformByteSize(
        16u, PipelineShaderStageFragment);
    temporalUpscalePass->setParameterProvider(
        ParameterProviders::TemporalUpscale);
    temporalUpscalePass->setUniformByteSize(
        16u, PipelineShaderStageFragment);
    motionBlurPass->setParameterProvider(
        ParameterProviders::MotionBlur);
    outlineTemporalPass->setParameterProvider(
        ParameterProviders::OutlineTemporal);
    postProcessPass->setParameterProvider(
        ParameterProviders::FinalComposite);

    for (uint32_t cascade = 0; cascade < ShadowCascadeCount; ++cascade) {
        shadowPasses[cascade]->setDepthAttachment(
            "ShadowMap" + std::to_string(cascade));
        virtualShadowPasses[cascade]->setDepthAttachment(
            "VirtualShadowAtlas",
            cascade == 0 ? AttachmentLoad::Clear : AttachmentLoad::Load,
            AttachmentStore::Store,
            false,
            1.0f,
            cascade == 0
                ? std::string{}
                : "VirtualShadowPage" + std::to_string(cascade - 1));
    }

    gBufferPass->addColorAttachment("GBufferAlbedo");
    gBufferPass->addColorAttachment("GBufferNormal");
    gBufferPass->addColorAttachment("GBufferMaterial");
    gBufferPass->addColorAttachment("GBufferWorldPos");
    gBufferPass->addColorAttachment("GBufferEffects");
    gBufferPass->addColorAttachment("GBufferVelocity");
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

    if (config_.shadowTechnique == DeferredShadowTechnique::VirtualShadowMap) {
        for (const auto& [slot, binding] : std::array{
                 std::pair{"shadowMap0", 1u},
                 std::pair{"shadowMap1", 12u},
                 std::pair{"shadowMap2", 13u},
                 std::pair{"shadowMap3", 14u}}) {
            lightingPass->addSampledTexture(
                slot, "VirtualShadowAtlas", binding, false,
                "VirtualShadowPage3");
        }
        lightingPass->addSampledTexture(
            "virtualShadowAtlas", "VirtualShadowAtlas", 15, false,
            "VirtualShadowPage3");
    } else {
        const uint32_t activeCascadeCount =
            config_.shadowTechnique == DeferredShadowTechnique::ShadowMap
                ? 1u
                : ShadowCascadeCount;
        for (uint32_t cascade = 0; cascade < ShadowCascadeCount; ++cascade) {
            const uint32_t sourceCascade =
                std::min(cascade, activeCascadeCount - 1u);
            lightingPass->addSampledTexture(
                "shadowMap" + std::to_string(cascade),
                "ShadowMap" + std::to_string(sourceCascade),
                cascade == 0 ? 1u : 11u + cascade,
                false,
                "ShadowCascade" + std::to_string(sourceCascade));
        }
        lightingPass->addSampledTexture(
            "virtualShadowAtlas", "ShadowMap0", 15, false,
            "ShadowCascade0");
    }
    lightingPass->addSampledTexture("gBufferAlbedo", "GBufferAlbedo", 2);
    lightingPass->addSampledTexture("gBufferNormal", "GBufferNormal", 3);
    lightingPass->addSampledTexture("gBufferMaterial", "GBufferMaterial", 4);
    lightingPass->addSampledTexture("gBufferEffects", "GBufferEffects", 5);
    lightingPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 6);
    lightingPass->addSampledTexture("sceneDepth", "SceneDepth", 7);
    lightingPass->addSampledTexture(
        "hbaoTexture",
        config_.hbao ? "HBAO" : "GBufferMaterial",
        11,
        false,
        config_.hbao ? "HBAO" : "GBuffer");
    lightingPass->addColorAttachment("SceneColor");

    transparentPass->addColorAttachment(
        "SceneColor",
        AttachmentLoad::Load,
        AttachmentStore::Store,
        "Lighting");
    transparentPass->setDepthAttachment(
        "SceneDepth", AttachmentLoad::Load, AttachmentStore::Store, true);

    std::string currentHdrResource = "SceneColor";
    std::string currentHdrProducer = "Transparent";

    ssrPass->addSampledTexture(
        "sceneColor", currentHdrResource, 1, false, currentHdrProducer);
    ssrPass->addSampledTexture("gBufferNormal", "GBufferNormal", 2);
    ssrPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 3);
    ssrPass->addSampledTexture("sceneDepth", "SceneDepth", 4);
    ssrPass->addSampledTexture("gBufferMaterial", "GBufferMaterial", 5);
    ssrPass->addSampledTexture("hiZHalf", "HiZHalf", 6);
    ssrPass->addSampledTexture("hiZQuarter", "HiZQuarter", 7);
    ssrPass->addSampledTexture("hiZEighth", "HiZEighth", 8);
    ssrPass->addSampledTexture("hiZSixteenth", "HiZSixteenth", 9);
    ssrPass->addColorAttachment("SceneColorSSR");
    if (config_.ssr) {
        currentHdrResource = "SceneColorSSR";
        currentHdrProducer = "SSR";
    }

    depthOfFieldPass->addSampledTexture(
        "sceneColor", currentHdrResource, 1, false, currentHdrProducer);
    depthOfFieldPass->addSampledTexture("sceneDepth", "SceneDepth", 2);
    depthOfFieldPass->addSampledTexture("gBufferWorldPos", "GBufferWorldPos", 3);
    depthOfFieldPass->addColorAttachment("SceneColorDOF");
    if (config_.depthOfField) {
        currentHdrResource = "SceneColorDOF";
        currentHdrProducer = "DepthOfField";
    }

    // Both temporal implementations are described here, but only the selected
    // one is inserted into the graph below. The HDR dependency chain is fixed
    // before RenderGraph compilation and is never rewritten during execution.
    temporalAaPass->addSampledTexture(
        "sceneColor", currentHdrResource, 1, false, currentHdrProducer);
    temporalAaPass->addSampledTexture("sceneDepth", "SceneDepth", 2);
    temporalAaPass->addSampledTexture("taaHistoryColor", "TAAColor", 3, true);
    temporalAaPass->addSampledTexture(
        "taaHistoryData", "TemporalHistoryData", 4, true);
    temporalAaPass->addSampledTexture("gBufferVelocity", "GBufferVelocity", 5);
    temporalAaPass->addSampledTexture("gBufferNormal", "GBufferNormal", 6);
    temporalAaPass->addSampledTexture("gBufferMaterial", "GBufferMaterial", 7);
    temporalAaPass->addColorAttachment("TAAColor");
    temporalAaPass->addColorAttachment("TemporalHistoryData");

    temporalUpscalePass->addSampledTexture(
        "sceneColor", currentHdrResource, 1, false, currentHdrProducer);
    temporalUpscalePass->addSampledTexture("sceneDepth", "SceneDepth", 2);
    temporalUpscalePass->addSampledTexture(
        "taaHistoryColor", "TemporalColor", 3, true);
    temporalUpscalePass->addSampledTexture(
        "taaHistoryData", "TemporalHistoryData", 4, true);
    temporalUpscalePass->addSampledTexture(
        "gBufferVelocity", "GBufferVelocity", 5);
    temporalUpscalePass->addSampledTexture("gBufferNormal", "GBufferNormal", 6);
    temporalUpscalePass->addSampledTexture(
        "gBufferMaterial", "GBufferMaterial", 7);
    temporalUpscalePass->addColorAttachment("TemporalColor");
    temporalUpscalePass->addColorAttachment("TemporalHistoryData");

    if (config_.temporalMode == 1) {
        currentHdrResource = "TAAColor";
        currentHdrProducer = "TemporalAA";
    } else if (config_.temporalMode == 2) {
        currentHdrResource = "TemporalColor";
        currentHdrProducer = "TemporalUpscale";
    }

    motionBlurPass->addSampledTexture(
        "sceneColor", currentHdrResource, 1, false, currentHdrProducer);
    motionBlurPass->addSampledTexture("gBufferVelocity", "GBufferVelocity", 2);
    motionBlurPass->addSampledTexture("sceneDepth", "SceneDepth", 3);
    motionBlurPass->addColorAttachment("MotionBlurColor");
    if (config_.motionBlur) {
        currentHdrResource = "MotionBlurColor";
        currentHdrProducer = "MotionBlur";
    }

    bloomDownHalfPass->addSampledTexture(
        "sourceTexture", currentHdrResource, 1, false, currentHdrProducer);
    bloomDownHalfPass->addColorAttachment("BloomDownHalf");
    bloomDownQuarterPass->addSampledTexture(
        "sourceTexture", "BloomDownHalf", 1);
    bloomDownQuarterPass->addColorAttachment("BloomDownQuarter");
    bloomDownEighthPass->addSampledTexture(
        "sourceTexture", "BloomDownQuarter", 1);
    bloomDownEighthPass->addColorAttachment("BloomDownEighth");
    bloomDownSixteenthPass->addSampledTexture(
        "sourceTexture", "BloomDownEighth", 1);
    bloomDownSixteenthPass->addColorAttachment("BloomDownSixteenth");

    bloomUpEighthPass->addSampledTexture(
        "lowerBloom", "BloomDownSixteenth", 1);
    bloomUpEighthPass->addSampledTexture(
        "currentBloom", "BloomDownEighth", 2);
    bloomUpEighthPass->addColorAttachment("BloomUpEighth");
    bloomUpQuarterPass->addSampledTexture(
        "lowerBloom", "BloomUpEighth", 1);
    bloomUpQuarterPass->addSampledTexture(
        "currentBloom", "BloomDownQuarter", 2);
    bloomUpQuarterPass->addColorAttachment("BloomUpQuarter");
    bloomUpHalfPass->addSampledTexture(
        "lowerBloom", "BloomUpQuarter", 1);
    bloomUpHalfPass->addSampledTexture(
        "currentBloom", "BloomDownHalf", 2);
    bloomUpHalfPass->addColorAttachment("BloomLowRes");

    outlineTemporalPass->addSampledTexture(
        "gBufferNormal", "GBufferNormal", 1);
    outlineTemporalPass->addSampledTexture(
        "gBufferVelocity", "GBufferVelocity", 2);
    outlineTemporalPass->addSampledTexture(
        "outlineHistory", "OutlineHistory", 3, true);
    outlineTemporalPass->addSampledTexture(
        "sceneDepth", "SceneDepth", 4);
    outlineTemporalPass->addColorAttachment("OutlineHistory");

    // Temporal reconstruction stabilizes the jittered HDR scene first.
    // Bloom is then extracted from that stable result before the final
    // outline, tone-map and composition pass.
    postProcessPass->addSampledTexture(
        "sceneColor", currentHdrResource, 1, false, currentHdrProducer);
    postProcessPass->addSampledTexture("gBufferNormal", "GBufferNormal", 2);
    postProcessPass->addSampledTexture(
        "bloomLowRes",
        config_.bloom ? "BloomLowRes" : currentHdrResource,
        3,
        false,
        config_.bloom ? "BloomUpHalf" : currentHdrProducer);
    postProcessPass->addSampledTexture(
        "outlineMask",
        config_.outline ? "OutlineHistory" : "GBufferNormal",
        4,
        false,
        config_.outline ? "OutlineTemporal" : "GBuffer");
    postProcessPass->addSampledTexture(
        "gBufferWorldPos", "GBufferWorldPos", 5);
    postProcessPass->addColorAttachment("FinalColor");

    addDeferredGBufferTextures(gBufferPass);
    addDeferredMaterialTextures(transparentPass);
    uint32_t transparentIblBinding = 1;
    for (const auto& requirement : transparentPass->getMaterialTextures()) {
        transparentIblBinding = std::max(
            transparentIblBinding, requirement.binding + 1u);
    }
    transparentPass->addImportedTexture({
        transparentIblBinding, ImportedResourceHandles::IblIrradiance,
        PipelineShaderStageFragment
    });
    transparentPass->addImportedTexture({
        transparentIblBinding + 1u, ImportedResourceHandles::IblPrefiltered,
        PipelineShaderStageFragment
    });
    transparentPass->addImportedTexture({
        transparentIblBinding + 2u, ImportedResourceHandles::IblBrdfLut,
        PipelineShaderStageFragment
    });

    for (auto& shadowPass : shadowPasses) {
        shadowPass->setVertexShader(Shader::create(
            "res/Shaders/Source/deferred_shadow.hlsl", ShaderType::Vertex));
        shadowPass->setFragmentShader(Shader::create(
            "res/Shaders/Source/deferred_shadow.hlsl", ShaderType::Fragment));
    }
    for (auto& shadowPass : virtualShadowPasses) {
        shadowPass->setVertexShader(Shader::create(
            "res/Shaders/Source/deferred_shadow.hlsl", ShaderType::Vertex));
        shadowPass->setFragmentShader(Shader::create(
            "res/Shaders/Source/deferred_shadow.hlsl", ShaderType::Fragment));
    }
    gBufferPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_gbuffer.hlsl", ShaderType::Vertex));
    gBufferPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_gbuffer.hlsl", ShaderType::Fragment));
    hbaoPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_hbao.hlsl", ShaderType::Vertex));
    hbaoPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_hbao.hlsl", ShaderType::Fragment));
    hiZInitPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_hiz_init.hlsl", ShaderType::Vertex));
    hiZInitPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_hiz_init.hlsl", ShaderType::Fragment));
    hiZQuarterPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_hiz_reduce.hlsl", ShaderType::Vertex));
    hiZQuarterPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_hiz_reduce.hlsl", ShaderType::Fragment));
    hiZEighthPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_hiz_reduce.hlsl", ShaderType::Vertex));
    hiZEighthPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_hiz_reduce.hlsl", ShaderType::Fragment));
    hiZSixteenthPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_hiz_reduce.hlsl", ShaderType::Vertex));
    hiZSixteenthPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_hiz_reduce.hlsl", ShaderType::Fragment));
    lightingPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_lighting.hlsl", ShaderType::Vertex));
    lightingPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_lighting.hlsl", ShaderType::Fragment));
    transparentPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_transparent.hlsl", ShaderType::Vertex));
    transparentPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_transparent.hlsl", ShaderType::Fragment));
    for (const auto& bloomDownPass : {
             bloomDownHalfPass,
             bloomDownQuarterPass,
             bloomDownEighthPass,
             bloomDownSixteenthPass}) {
        bloomDownPass->setVertexShader(Shader::create(
            "res/Shaders/Source/deferred_bloom_downsample.hlsl",
            ShaderType::Vertex));
        bloomDownPass->setFragmentShader(Shader::create(
            "res/Shaders/Source/deferred_bloom_downsample.hlsl",
            ShaderType::Fragment));
    }
    for (const auto& bloomUpPass : {
             bloomUpEighthPass,
             bloomUpQuarterPass,
             bloomUpHalfPass}) {
        bloomUpPass->setVertexShader(Shader::create(
            "res/Shaders/Source/deferred_bloom_upsample.hlsl",
            ShaderType::Vertex));
        bloomUpPass->setFragmentShader(Shader::create(
            "res/Shaders/Source/deferred_bloom_upsample.hlsl",
            ShaderType::Fragment));
    }
    ssrPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_ssr.hlsl", ShaderType::Vertex));
    ssrPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_ssr.hlsl", ShaderType::Fragment));
    depthOfFieldPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_dof.hlsl", ShaderType::Vertex));
    depthOfFieldPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_dof.hlsl", ShaderType::Fragment));
    temporalAaPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_taau.hlsl", ShaderType::Vertex));
    temporalAaPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_taau.hlsl", ShaderType::Fragment));
    temporalUpscalePass->setVertexShader(Shader::create("res/Shaders/Source/deferred_taau.hlsl", ShaderType::Vertex));
    temporalUpscalePass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_taau.hlsl", ShaderType::Fragment));
    motionBlurPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_motion_blur.hlsl", ShaderType::Vertex));
    motionBlurPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_motion_blur.hlsl", ShaderType::Fragment));
    outlineTemporalPass->setVertexShader(Shader::create(
        "res/Shaders/Source/deferred_outline_temporal.hlsl",
        ShaderType::Vertex));
    outlineTemporalPass->setFragmentShader(Shader::create(
        "res/Shaders/Source/deferred_outline_temporal.hlsl",
        ShaderType::Fragment));
    postProcessPass->setVertexShader(Shader::create("res/Shaders/Source/deferred_postprocess.hlsl", ShaderType::Vertex));
    postProcessPass->setFragmentShader(Shader::create("res/Shaders/Source/deferred_postprocess.hlsl", ShaderType::Fragment));
    for (uint32_t permutation = 0; permutation < 8u; ++permutation) {
        postProcessPass->addShaderPermutation({
            permutation,
            postProcessPass->getVertexShader(),
            Shader::create(
                "res/Shaders/Source/deferred_postprocess.hlsl",
                ShaderType::Fragment,
                permutation),
            nullptr
        });
    }

    switch (config_.shadowTechnique) {
    case DeferredShadowTechnique::ShadowMap:
        addPass(shadowPasses.front());
        break;
    case DeferredShadowTechnique::CascadedShadowMap:
        for (const auto& shadowPass : shadowPasses) {
            addPass(shadowPass);
        }
        break;
    case DeferredShadowTechnique::VirtualShadowMap:
        for (const auto& shadowPass : virtualShadowPasses) {
            addPass(shadowPass);
        }
        break;
    }
    addPass(gBufferPass);
    if (config_.hbao) {
        addPass(hbaoPass);
    }
    if (config_.hiZ) {
        addPass(hiZInitPass);
        addPass(hiZQuarterPass);
        addPass(hiZEighthPass);
        addPass(hiZSixteenthPass);
    }
    addPass(lightingPass);
    addPass(transparentPass);
    if (config_.ssr) {
        addPass(ssrPass);
    }
    if (config_.depthOfField) {
        addPass(depthOfFieldPass);
    }
    if (config_.temporalMode == 1) {
        addPass(temporalAaPass);
    } else if (config_.temporalMode == 2) {
        addPass(temporalUpscalePass);
    }
    if (config_.motionBlur) {
        addPass(motionBlurPass);
    }
    if (config_.outline) {
        addPass(outlineTemporalPass);
    }
    if (config_.bloom) {
        addPass(bloomDownHalfPass);
        addPass(bloomDownQuarterPass);
        addPass(bloomDownEighthPass);
        addPass(bloomDownSixteenthPass);
        addPass(bloomUpEighthPass);
        addPass(bloomUpQuarterPass);
        addPass(bloomUpHalfPass);
    }
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
                if (config_.shadowTechnique ==
                    DeferredShadowTechnique::VirtualShadowMap) {
                    for (const auto& shadowPass : virtualShadowPasses) {
                        shadowPass->addObject(object);
                    }
                } else {
                    const size_t shadowPassCount =
                        config_.shadowTechnique ==
                                DeferredShadowTechnique::ShadowMap
                            ? 1u
                            : shadowPasses.size();
                    for (size_t i = 0; i < shadowPassCount; ++i) {
                        shadowPasses[i]->addObject(object);
                    }
                }
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
