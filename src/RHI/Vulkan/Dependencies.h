#pragma once
#include <volk.h>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "VulkanPipeline.h"
#include "ImmediateSubmitter.h"
#include "Renderer.h"
#include "VulkanQueue.h"
#include "Model.h"
#include "DescriptorWriter.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanDescriptorPool.h"
#include "IBLMap.h"

struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 lightDir;
        glm::vec4 lightColor;
        glm::vec4 camPos;
		float metallic;
		float roughness;
		float ao;
};