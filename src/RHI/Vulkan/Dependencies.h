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
#include "TSVector.h"
#include "TSMatrix.h"

struct UniformBufferObject {
        Tasrovy::TSMat4f model;
        Tasrovy::TSMat4f view;
        Tasrovy::TSMat4f proj;
        Tasrovy::TSVec4f lightDir;
        Tasrovy::TSVec4f lightColor;
        Tasrovy::TSVec4f camPos;
		float metallic;
		float roughness;
		float ao;
};