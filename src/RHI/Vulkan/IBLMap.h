#pragma once
#include "Dependencies.h"
#include <unordered_map>

struct IBTextures {
    std::unique_ptr<VulkanImage> irradianceMap;
    std::unique_ptr<VulkanImage> prefilteredMap;
};

class IBLProcessor {
public:
    IBLProcessor(VulkanContext& context, ImmediateSubmitter& uploader);
    ~IBLProcessor();

    IBLProcessor(const IBLProcessor&) = delete;
    IBLProcessor& operator=(const IBLProcessor&) = delete;

    void generateIrradianceMap(VulkanImage& environmentCubemap,VulkanImage& irradianceMap);
    void generatePrefilteredMap(VulkanImage& environmentCubemap, VulkanImage& prefilteredMap);
    void generateBrdfMap();
    void addSkybox(VulkanImage& skybox,std::string name);
    VulkanImage* getIrradianceMap(const std::string& name) const;
    VulkanImage* getPrefilteredMap(const std::string& name) const;
    VulkanImage* getBrdfLUT() const;
private:
    void createPipelines();

    VulkanContext& _context;
    ImmediateSubmitter& _uploader;

    std::unique_ptr<VulkanDescriptorSetLayout> _equirectToCubeLayout;
    std::unique_ptr<VulkanPipeline> _equirectToCubePipeline;

    std::unique_ptr<VulkanDescriptorSetLayout> _irradianceLayout;
    std::unique_ptr<VulkanPipeline> _irradiancePipeline;

    std::unique_ptr<VulkanDescriptorSetLayout> _prefilterLayout;
    std::unique_ptr<VulkanPipeline> _prefilterPipeline;

    std::unique_ptr<VulkanDescriptorSetLayout> _brdfLutLayout;
    std::unique_ptr<VulkanPipeline> _brdfLutPipeline;

    std::unique_ptr<VulkanImage> _brdfLUT;

    std::unordered_map<std::string, IBTextures> _ibTextures;
};
