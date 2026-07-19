#ifndef TASROVY_POSTPROCESS_TONEMAP
#define TASROVY_POSTPROCESS_TONEMAP

float3 ApplyExposureToneMap(float3 color, float exposure)
{
    // The swapchain is sRGB, so this function only maps linear HDR to linear
    // display range. The attachment performs the final display encoding.
    return 1.0f.xxx - exp(-max(color, 0.0f.xxx) * max(exposure, 0.0001f));
}

#endif
