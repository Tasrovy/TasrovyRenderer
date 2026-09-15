#include <volk.h>
#include <nvsdk_ngx_defs_vk.h>
#include <nvsdk_ngx_params.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>

namespace {

using InitExt2 = NVSDK_NGX_Result (NVSDK_CONV *)(
    unsigned long long, const wchar_t*, VkInstance, VkPhysicalDevice,
    VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
    NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
using CreateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(
    VkCommandBuffer, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*,
    NVSDK_NGX_Handle**);
using EvaluateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(
    VkCommandBuffer, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    void*);
using ReleaseFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle*);
using Shutdown = NVSDK_NGX_Result (NVSDK_CONV *)(VkDevice);
using SetUnsignedLongLong = void (NVSDK_CONV *)(
    void*, const char*, unsigned long long);
using SetUnsignedInt = void (NVSDK_CONV *)(
    void*, const char*, unsigned int);
using SetFloat = void (NVSDK_CONV *)(void*, const char*, float);
using GetUnsignedInt = NVSDK_NGX_Result (NVSDK_CONV *)(
    const void*, const char*, unsigned int*);
using GetFloat = NVSDK_NGX_Result (NVSDK_CONV *)(
    const void*, const char*, float*);

struct SnippetApi {
    HMODULE module = nullptr;
    InitExt2 init = nullptr;
    CreateFeature create = nullptr;
    EvaluateFeature evaluate = nullptr;
    ReleaseFeature release = nullptr;
    Shutdown shutdown = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    bool initialized = false;
};

SnippetApi api;
int unsignedSetterSlot = 3;
int floatSetterSlot = 6;
bool setterSlotsDiscovered = false;
NVSDK_NGX_Result lastInitResult = NVSDK_NGX_Result_FAIL_NotInitialized;
NVSDK_NGX_Result lastCreateResult = NVSDK_NGX_Result_FAIL_NotInitialized;

template <typename Function>
Function loadFunction(const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(api.module, name));
}

bool loadSnippet(const wchar_t* runtimePath) {
    if (api.module)
        return api.create && api.evaluate;
    api.module = LoadLibraryExW(
        runtimePath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!api.module)
        return false;
    api.init = loadFunction<InitExt2>("NVSDK_NGX_VULKAN_Init_Ext2");
    api.create = loadFunction<CreateFeature>(
        "NVSDK_NGX_VULKAN_CreateFeature");
    api.evaluate = loadFunction<EvaluateFeature>(
        "NVSDK_NGX_VULKAN_EvaluateFeature");
    api.release = loadFunction<ReleaseFeature>(
        "NVSDK_NGX_VULKAN_ReleaseFeature");
    api.shutdown = loadFunction<Shutdown>("NVSDK_NGX_VULKAN_Shutdown1");
    return api.init && api.create && api.evaluate && api.release;
}

void setUnsigned(NVSDK_NGX_Parameter* parameters, const char* name,
                 unsigned int value) {
    void** vtable = *reinterpret_cast<void***>(parameters);
    reinterpret_cast<SetUnsignedInt>(vtable[unsignedSetterSlot])(
        parameters, name, value);
}

void setFloat(NVSDK_NGX_Parameter* parameters, const char* name, float value) {
    void** vtable = *reinterpret_cast<void***>(parameters);
    reinterpret_cast<SetFloat>(vtable[floatSetterSlot])(
        parameters, name, value);
}

void setResource(NVSDK_NGX_Parameter* parameters, const char* name,
                 NVSDK_NGX_Resource_VK* resource) {
    void** vtable = *reinterpret_cast<void***>(parameters);
    reinterpret_cast<SetUnsignedLongLong>(vtable[0])(
        parameters, name,
        reinterpret_cast<unsigned long long>(resource));
}

void discoverSetterSlots(NVSDK_NGX_Parameter* parameters) {
    if (setterSlotsDiscovered)
        return;
    void** vtable = *reinterpret_cast<void***>(parameters);
    const auto getUnsigned = reinterpret_cast<GetUnsignedInt>(vtable[12]);
    const auto getFloat = reinterpret_cast<GetFloat>(vtable[14]);
    for (int slot = 0; slot < 8; ++slot) {
        constexpr unsigned int probe = 0x1234u;
        unsigned int value = 0;
        reinterpret_cast<SetUnsignedInt>(vtable[slot])(
            parameters, "DLSSNR.UIntSetterProbe", probe);
        if (NVSDK_NGX_SUCCEED(getUnsigned(
                parameters, "DLSSNR.UIntSetterProbe", &value)) &&
            value == probe) {
            unsignedSetterSlot = slot;
            break;
        }
    }
    for (int slot = 0; slot < 8; ++slot) {
        constexpr float probe = 0.3125f;
        float value = 0.0f;
        reinterpret_cast<SetFloat>(vtable[slot])(
            parameters, "DLSSNR.FloatSetterProbe", probe);
        if (NVSDK_NGX_SUCCEED(getFloat(
                parameters, "DLSSNR.FloatSetterProbe", &value)) &&
            value == probe) {
            floatSetterSlot = slot;
            break;
        }
    }
    setterSlotsDiscovered = true;
}

void setExtent(
    NVSDK_NGX_Parameter* parameters, uint32_t width, uint32_t height) {
    setUnsigned(parameters, "Width", width);
    setUnsigned(parameters, "Height", height);
    setUnsigned(parameters, "OutWidth", width);
    setUnsigned(parameters, "OutHeight", height);
    setUnsigned(parameters, "PerfQualityValue", 2u);
    setUnsigned(parameters, "CreationNodeMask", 1u);
    setUnsigned(parameters, "VisibilityNodeMask", 1u);
    setUnsigned(parameters, "DLSSNR.Enabled", 1u);
    setUnsigned(parameters, "DLSSNR.Width", width);
    setUnsigned(parameters, "DLSSNR.Height", height);
    setUnsigned(parameters, "DLSSNR.InputWidth", width);
    setUnsigned(parameters, "DLSSNR.InputHeight", height);
    setUnsigned(parameters, "DLSSNR.OutputWidth", width);
    setUnsigned(parameters, "DLSSNR.OutputHeight", height);
    setUnsigned(parameters, "DLSSNR.Upscaling", 0u);
    setFloat(parameters, "DLSSNR.Scale", 1.0f);
    setFloat(parameters, "DLSSNR.ScalingRatio", 1.0f);
}

} // namespace

extern "C" {

__declspec(dllexport) NVSDK_NGX_Result tasrovyDlssNrVulkanCreate(
    const wchar_t* runtimePath,
    const wchar_t* applicationDataPath,
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    PFN_vkGetInstanceProcAddr getInstanceProcAddress,
    PFN_vkGetDeviceProcAddr getDeviceProcAddress,
    VkCommandBuffer commandBuffer,
    NVSDK_NGX_Parameter* parameters,
    uint32_t width,
    uint32_t height,
    uint32_t style,
    float intensity,
    float localTone,
    float localStructure,
    float skinStructure,
    uint32_t autoMask,
    uint32_t uiCorrection,
    NVSDK_NGX_Handle** feature) {
    if (!runtimePath || !parameters || !feature ||
        !loadSnippet(runtimePath)) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    discoverSetterSlots(parameters);
    if (!api.initialized) {
        lastInitResult = api.init(
            0x24480451ull,
            applicationDataPath,
            instance,
            physicalDevice,
            device,
            getInstanceProcAddress,
            getDeviceProcAddress,
            NVSDK_NGX_Version_API,
            parameters);
        if (!NVSDK_NGX_SUCCEED(lastInitResult))
            return lastInitResult;
        api.initialized = true;
        api.device = device;
    }

    setExtent(parameters, width, height);
    setUnsigned(parameters, "DLSSNR.Hint.Render.Preset", 0u);
    setUnsigned(parameters, "DLSSNR.Style", style);
    setFloat(parameters, "DLSSNR.Intensity", intensity);
    setFloat(parameters, "DLSSNR.LocalToneStrength", localTone);
    setFloat(parameters, "DLSSNR.LocalStructureStrength", localStructure);
    if (skinStructure >= 0.0f)
        setFloat(parameters, "DLSSNR.SkinStructureStrength", skinStructure);
    setUnsigned(parameters, "DLSSNR.UseAutoMask", autoMask);
    setUnsigned(parameters, "DLSSNR.UICorrection", uiCorrection);
    // Keep this stack frame. The NR snippet validates the module containing
    // its caller's return address; a tail call would incorrectly expose the
    // renderer executable instead of this nvngx.dll-named bridge.
    volatile NVSDK_NGX_Result result = api.create(
        commandBuffer,
        static_cast<NVSDK_NGX_Feature>(18),
        parameters,
        feature);
    lastCreateResult = result;
    return result;
}

__declspec(dllexport) NVSDK_NGX_Result tasrovyDlssNrVulkanEvaluate(
    VkCommandBuffer commandBuffer,
    NVSDK_NGX_Handle* feature,
    NVSDK_NGX_Parameter* parameters,
    NVSDK_NGX_Resource_VK* color,
    NVSDK_NGX_Resource_VK* depth,
    NVSDK_NGX_Resource_VK* motion,
    NVSDK_NGX_Resource_VK* output,
    uint32_t width,
    uint32_t height,
    float motionScaleX,
    float motionScaleY,
    uint32_t depthInverted,
    uint32_t reset,
    uint32_t style,
    float intensity,
    float localTone,
    float localStructure,
    float skinStructure,
    uint32_t autoMask,
    uint32_t uiCorrection) {
    if (!api.evaluate || !feature || !parameters)
        return NVSDK_NGX_Result_FAIL_NotInitialized;
    setResource(parameters, "DLSSNR.Color", color);
    setResource(parameters, "DLSSNR.Depth", depth);
    setResource(parameters, "DLSSNR.MVec", motion);
    setResource(parameters, "DLSSNR.Output", output);
    setUnsigned(parameters, "DLSSNR.DepthInverted", depthInverted);
    setUnsigned(parameters, "DLSSNR.Reset", reset);
    setFloat(parameters, "DLSSNR.MVecScaleX", motionScaleX);
    setFloat(parameters, "DLSSNR.MVecScaleY", motionScaleY);
    // These are runtime controls, not Feature identity. Update them before
    // every evaluation so changing a UI slider does not recreate the model.
    setUnsigned(parameters, "DLSSNR.Style", style);
    setFloat(parameters, "DLSSNR.Intensity", intensity);
    setFloat(parameters, "DLSSNR.LocalToneStrength", localTone);
    setFloat(parameters, "DLSSNR.LocalStructureStrength", localStructure);
    if (skinStructure >= 0.0f)
        setFloat(parameters, "DLSSNR.SkinStructureStrength", skinStructure);
    setUnsigned(parameters, "DLSSNR.UseAutoMask", autoMask);
    setUnsigned(parameters, "DLSSNR.UICorrection", uiCorrection);
    for (const char* prefix : {"Color", "Depth", "MVec", "Output"}) {
        const auto base = std::string("DLSSNR.") + prefix;
        setUnsigned(parameters, (base + "SubrectBaseX").c_str(), 0u);
        setUnsigned(parameters, (base + "SubrectBaseY").c_str(), 0u);
        setUnsigned(parameters, (base + "SubrectWidth").c_str(), width);
        setUnsigned(parameters, (base + "SubrectHeight").c_str(), height);
    }
    // See the CreateFeature note above: prevent tail-call elimination.
    volatile NVSDK_NGX_Result result =
        api.evaluate(commandBuffer, feature, parameters, nullptr);
    return result;
}

__declspec(dllexport) void tasrovyDlssNrVulkanRelease(
    NVSDK_NGX_Handle* feature) {
    if (feature && api.release)
        api.release(feature);
}

__declspec(dllexport) void tasrovyDlssNrVulkanShutdown() {
    if (api.initialized && api.shutdown)
        api.shutdown(api.device);
    api.initialized = false;
    api.device = VK_NULL_HANDLE;
    if (api.module)
        FreeLibrary(api.module);
    api = {};
}

__declspec(dllexport) uint32_t tasrovyDlssNrVulkanLastInitResult() {
    return static_cast<uint32_t>(lastInitResult);
}

__declspec(dllexport) uint32_t tasrovyDlssNrVulkanLastCreateResult() {
    return static_cast<uint32_t>(lastCreateResult);
}

} // extern "C"
