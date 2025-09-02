#ifdef SHADER_CLOCK
#define START_SHADERCLOCK() const uint64_t clock = Bindless.GetGpuscene().Camera[0].ShowHeatmap ? clockARB() : 0;

#define END_SHADERCLOCK(ipos)                                                              \
    if (Bindless.GetGpuscene().Camera[0].ShowHeatmap)                                     \
    {                                                                                      \
        const uint64_t deltaTime = clockARB() - clock;                                     \
        const float heatmapScale = 1000000.0f * Bindless.GetGpuscene().Camera[0].HeatmapScale * Bindless.GetGpuscene().Camera[0].HeatmapScale; \
        const float deltaTimeScaled = clamp(float(deltaTime) / heatmapScale, 0.0f, 1.0f);  \
        const float4 shaderHeatColor = float4(heatmap(deltaTimeScaled), 1.0);              \
        Bindless.GetStorageTexture<float4>(Bindless.RT_SHADER_TIMER)[ipos] = shaderHeatColor;                                         \
    }
#else
#define START_SHADERCLOCK()
#define END_SHADERCLOCK(ipos)
#endif