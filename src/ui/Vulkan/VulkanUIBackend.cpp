#include "VulkanUIBackend.h"

#include "../../RHI/Device.h"
#include "../../RHI/Vulkan/VulkanConversions.h"
#include "../../log/Logger.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <volk.h>

#include <cstdlib>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Tasrovy::UI {
namespace {

void checkVkResult(VkResult result) {
    if (result == VK_SUCCESS) return;
    LOG_ERROR("[vulkan] VkResult = {}", static_cast<int>(result));
    if (result < 0) std::abort();
}

} // namespace

struct VulkanUIBackend::CapturedFrame {
    ImDrawData drawData;
    std::vector<ImDrawList*> ownedLists;

    explicit CapturedFrame(const ImDrawData& source) {
        drawData.Valid = source.Valid;
        drawData.DisplayPos = source.DisplayPos;
        drawData.DisplaySize = source.DisplaySize;
        drawData.FramebufferScale = source.FramebufferScale;
        drawData.OwnerViewport = source.OwnerViewport;
        drawData.Textures = source.Textures;
        ownedLists.reserve(source.CmdLists.Size);
        for (const ImDrawList* sourceList : source.CmdLists) {
            ImDrawList* clone = sourceList->CloneOutput();
            ownedLists.push_back(clone);
            // CloneOutput intentionally omits ImDrawList's active writer
            // cursors, so AddDrawList() would validate it as a live builder.
            // Populate the finished ImDrawData aggregate directly instead.
            drawData.CmdLists.push_back(clone);
            ++drawData.CmdListsCount;
            drawData.TotalVtxCount += clone->VtxBuffer.Size;
            drawData.TotalIdxCount += clone->IdxBuffer.Size;
        }
    }

    ~CapturedFrame() {
        for (ImDrawList* list : ownedLists) {
            IM_DELETE(list);
        }
    }
};

VulkanUIBackend::VulkanUIBackend(
    GLFWwindow& window,
    Tasrovy::RHI::Device& rhiDevice,
    std::shared_ptr<std::mutex> frameMutex)
    : frameMutex_(std::move(frameMutex)) {
    const auto context = rhiDevice.getBackendInteropContext();
    if (context.api != Tasrovy::RHI::GraphicsAPI::Vulkan) {
        throw std::invalid_argument(
            "VulkanUIBackend requires a Vulkan RHI device");
    }

    const auto instance = reinterpret_cast<VkInstance>(context.handles[0]);
    const auto physicalDevice = reinterpret_cast<VkPhysicalDevice>(context.handles[1]);
    const auto device = reinterpret_cast<VkDevice>(context.handles[2]);
    const auto graphicsQueue = reinterpret_cast<VkQueue>(context.handles[3]);
    const auto colorFormat = Tasrovy::RHI::Vulkan::toVkFormat(
        context.presentationFormat);
    device_ = reinterpret_cast<uintptr_t>(device);

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 10},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 10}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(
            device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }
    descriptorPool_ = reinterpret_cast<uintptr_t>(descriptorPool);

    ImGui_ImplGlfw_InitForVulkan(&window, true);
    static VkInstance loadedInstance = VK_NULL_HANDLE;
    static VkDevice loadedDevice = VK_NULL_HANDLE;
    loadedInstance = instance;
    loadedDevice = device;
    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_3,
        [](const char* name, void*) -> PFN_vkVoidFunction {
            auto address = vkGetInstanceProcAddr(loadedInstance, name);
            return address ? address : vkGetDeviceProcAddr(loadedDevice, name);
        },
        nullptr);

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    ImGui_ImplVulkan_InitInfo init{};
    init.Instance = instance;
    init.PhysicalDevice = physicalDevice;
    init.Device = device;
    init.QueueFamily = context.queueIndex;
    init.Queue = graphicsQueue;
    init.DescriptorPool = descriptorPool;
    init.MinImageCount = context.minImageCount;
    init.ImageCount = context.imageCount;
    init.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
    // UI records after the scene resolve and targets the single-sampled
    // swapchain image directly, so its dynamic-rendering pipeline must not
    // inherit the scene MSAA sample count.
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.UseDynamicRendering = true;
    init.CheckVkResultFn = checkVkResult;
    ImGui_ImplVulkan_Init(&init);

    const float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(
        glfwGetPrimaryMonitor());
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    ImGui::GetIO().ConfigDpiScaleFonts = true;
    ImGui::GetIO().ConfigDpiScaleViewports = true;
}

VulkanUIBackend::~VulkanUIBackend() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    vkDestroyDescriptorPool(
        reinterpret_cast<VkDevice>(device_),
        reinterpret_cast<VkDescriptorPool>(descriptorPool_),
        nullptr);
}

void VulkanUIBackend::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

uint64_t VulkanUIBackend::captureFrame() {
    ImDrawData* source = ImGui::GetDrawData();
    if (!source || !source->Valid || source->DisplaySize.x <= 0.0f ||
        source->DisplaySize.y <= 0.0f) {
        return 0;
    }
    const uint64_t token = nextFrameToken_++;
    frames_.emplace(token, std::make_unique<CapturedFrame>(*source));
    return token;
}

void VulkanUIBackend::discardFrame(uint64_t frameToken) {
    frames_.erase(frameToken);
}

Tasrovy::RHI::GraphicsAPI VulkanUIBackend::getGraphicsAPI() const {
    return Tasrovy::RHI::GraphicsAPI::Vulkan;
}

void* VulkanUIBackend::getRenderBackend() {
    return static_cast<Tasrovy::RHI::Vulkan::VulkanRenderOverlayBackend*>(
        this);
}

void VulkanUIBackend::recordDrawData(
    VkCommandBuffer commandBuffer, uint64_t frameToken) {
    std::scoped_lock lock(*frameMutex_);
    const auto found = frames_.find(frameToken);
    if (found == frames_.end()) {
        return;
    }
    auto frame = std::move(found->second);
    frames_.erase(found);
    ImGui_ImplVulkan_RenderDrawData(&frame->drawData, commandBuffer);
}

std::unique_ptr<IUIBackend> createUIBackend(
    GLFWwindow& window,
    Tasrovy::RHI::Device& device,
    std::shared_ptr<std::mutex> frameMutex) {
    return std::make_unique<VulkanUIBackend>(
        window, device, std::move(frameMutex));
}

} // namespace Tasrovy::UI
