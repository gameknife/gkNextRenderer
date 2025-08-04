#ifdef SHADER_CLOCK
#define START_SHADERCLOCK() const uint64_t clock = Camera.ShowHeatmap ? clockARB() : 0;

#define END_SHADERCLOCK(ipos)                                                              \
    if (Camera.ShowHeatmap)                                                                \
    {                                                                                      \
        const uint64_t deltaTime = clockARB() - clock;                                     \
        const float heatmapScale = 1000000.0f * Camera.HeatmapScale * Camera.HeatmapScale; \
        const float deltaTimeScaled = clamp(float(deltaTime) / heatmapScale, 0.0f, 1.0f);  \
        const float4 shaderHeatColor = float4(heatmap(deltaTimeScaled), 1.0);              \
        ShaderTimerBuffer[ipos] = shaderHeatColor;                                         \
    }
#else
#define START_SHADERCLOCK()
#define END_SHADERCLOCK(ipos)
#endif