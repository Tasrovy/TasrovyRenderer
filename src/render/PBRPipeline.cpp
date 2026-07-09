//
// Created by manin on 2026/7/5.
//

#include "PBRPipeline.h"

#include "Material.h"
#include "Mesh.h"
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

    void addPBRMaterialTextures(
        const std::shared_ptr<PipelinePass>& pass,
        bool includeOpacity) {
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
            "ShadowMap", PipelineTextureFormat::Depth32Float,
            PipelineTextureExtent::Fixed, 1.0f, 1.0f, 2048, 2048
        });
        declareTexture({
            "GBufferAlbedo", PipelineTextureFormat::RGBA8Unorm
        });
        declareTexture({
            "GBufferNormal", PipelineTextureFormat::RGBA16Float
        });
        declareTexture({
            "GBufferMaterial", PipelineTextureFormat::RGBA8Unorm
        });
        declareTexture({
            "SceneDepth", PipelineTextureFormat::Depth32Float
        });
        declareTexture({
            "HDRSceneColor", PipelineTextureFormat::RGBA16Float
        });
        declareTexture({
            "FinalColor", PipelineTextureFormat::Swapchain,
            PipelineTextureExtent::RenderRelative, 1.0f, 1.0f, 0, 0, true
        });

        auto shadowPass = createPass(
            "Shadow", PipelinePassType::Shadow, CullMode::Front,
            true, true, DepthTestMode::Less, BlendMode::Off);
        auto gBufferPass = createPass(
            "GBuffer", PipelinePassType::Geometry, CullMode::Back,
            true, true, DepthTestMode::Less, BlendMode::Off);
        auto lightingPass = createPass(
            "Lighting", PipelinePassType::Lighting, CullMode::None,
            false, false, DepthTestMode::Less, BlendMode::Off);
        auto skyboxPass = createPass(
            "Skybox", PipelinePassType::Skybox, CullMode::None,
            true, false, DepthTestMode::LessOrEqual, BlendMode::Off);
        auto forwardPass = createPass(
            "Forward", PipelinePassType::Generic, CullMode::Back,
            true, true, DepthTestMode::Less, BlendMode::Off);
        auto transparentPass = createPass(
            "Transparent", PipelinePassType::Transparent, CullMode::Back,
            true, false, DepthTestMode::Less, BlendMode::Alpha);
        auto postProcessingPass = createPass(
            "PostProcessing", PipelinePassType::PostProcess, CullMode::None,
            false, false, DepthTestMode::Less, BlendMode::Off);

        shadowPass->setDepthAttachment("ShadowMap");

        gBufferPass->addColorAttachment("GBufferAlbedo");
        gBufferPass->addColorAttachment("GBufferNormal");
        gBufferPass->addColorAttachment("GBufferMaterial");
        gBufferPass->setDepthAttachment("SceneDepth");

        lightingPass->addSampledTexture("shadowMap", "ShadowMap");
        lightingPass->addSampledTexture("gBufferAlbedo", "GBufferAlbedo");
        lightingPass->addSampledTexture("gBufferNormal", "GBufferNormal");
        lightingPass->addSampledTexture("gBufferMaterial", "GBufferMaterial");
        lightingPass->addSampledTexture("sceneDepth", "SceneDepth");
        lightingPass->addColorAttachment("HDRSceneColor");

        skyboxPass->addColorAttachment("FinalColor");

        forwardPass->addSampledTexture("shadowMap", "ShadowMap");
        forwardPass->addColorAttachment("FinalColor", AttachmentLoad::Load);

        transparentPass->addSampledTexture("shadowMap", "ShadowMap");
        transparentPass->addColorAttachment("HDRSceneColor", AttachmentLoad::Load);
        transparentPass->setDepthAttachment(
            "SceneDepth", AttachmentLoad::Load, AttachmentStore::Store, true);

        postProcessingPass->addSampledTexture("sceneColor", "HDRSceneColor");
        postProcessingPass->addColorAttachment("FinalColor");

        shadowPass->addMaterialTexture({
            MaterialTextureSemantic::Opacity,
            "opacityTexture",
            MaterialTextureColorSpace::Linear,
            MaterialTextureFallback::White
        });
        addPBRMaterialTextures(gBufferPass, true);
        addPBRMaterialTextures(forwardPass, true);
        addPBRMaterialTextures(transparentPass, true);

        skyboxPass->setVertexShader(Shader::create("res\\skyvert.spv", ShaderType::Vertex));
        skyboxPass->setFragmentShader(Shader::create("res\\skyfrag.spv", ShaderType::Fragment));
        shadowPass->setVertexShader(Shader::create("res\\shadow_min_vert.spv", ShaderType::Vertex));
        shadowPass->setFragmentShader(Shader::create("res\\shadow_min_frag.spv", ShaderType::Fragment));
        gBufferPass->setVertexShader(Shader::create("res\\gbuffer_min_vert.spv", ShaderType::Vertex));
        gBufferPass->setFragmentShader(Shader::create("res\\gbuffer_min_frag.spv", ShaderType::Fragment));
        lightingPass->setVertexShader(Shader::create("res\\fullscreen_min_vert.spv", ShaderType::Vertex));
        lightingPass->setFragmentShader(Shader::create("res\\fullscreen_min_frag.spv", ShaderType::Fragment));
        forwardPass->setVertexShader(Shader::create("res\\vert.spv", ShaderType::Vertex));
        forwardPass->setFragmentShader(Shader::create("res\\frag.spv", ShaderType::Fragment));
        transparentPass->setVertexShader(Shader::create("res\\forward_min_vert.spv", ShaderType::Vertex));
        transparentPass->setFragmentShader(Shader::create("res\\forward_min_frag.spv", ShaderType::Fragment));
        postProcessingPass->setVertexShader(Shader::create("res\\fullscreen_min_vert.spv", ShaderType::Vertex));
        postProcessingPass->setFragmentShader(Shader::create("res\\fullscreen_min_frag.spv", ShaderType::Fragment));

        addPass(shadowPass);
        addPass(gBufferPass);
        addPass(lightingPass);
        addPass(skyboxPass);
        addPass(forwardPass);
        addPass(transparentPass);
        addPass(postProcessingPass);

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
                    forwardPass->addObject(object);
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
}
