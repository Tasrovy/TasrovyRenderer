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
        glm::vec4 lightDir;   // 使用 vec4 以确保对齐, w 分量未使用
        glm::vec4 lightColor; // 使用 vec4 以确保对齐, w 分量是光的强度
        glm::vec4 camPos;     // 使用 vec4 以确保对齐, w 分量未使用
		float metallic;
		float roughness;
		float ao;
};