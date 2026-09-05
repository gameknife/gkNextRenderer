import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let scene = Bindless.GetGpuscene();
    uint slot = min(scene.CustomData0, 4u);

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    uint* counters = (uint*)softMeshShader.Counters;
    VkDrawIndirectCommand* drawArgs = (VkDrawIndirectCommand*)softMeshShader.DrawArgs;
    VkDispatchIndirectCommand* dispatchArgs = (VkDispatchIndirectCommand*)softMeshShader.DispatchArgs;

    uint totalTri = counters[slot * 2];
    uint itemCount = counters[slot * 2 + 1];
    uint triangleCapacity = scene.CustomData2;
    totalTri = min(totalTri, triangleCapacity);
    itemCount = min(itemCount, uint(MAX_RENDER_PROXIES));

    // FrameStamp is written here rather than in the cull pass: this dispatch is ordered after
    // every cull workgroup, so a non-zero stamp guarantees the whole block is populated. Stamping
    // from the cull itself let the first workgroup to finish mark a block the others had not
    // contributed to yet, and the CPU could sample that partial state.
    const uint stamp = scene.Camera[0].TotalFrames + 1u;
    if (slot == 0u)
    {
        scene.GPUDrivenStats[0].VisibleCount = itemCount;
        scene.GPUDrivenStats[0].FrameStamp = stamp;
    }
    else
    {
        scene.ShadowGPUDrivenStats[slot - 1u].VisibleCount = itemCount;
        scene.ShadowGPUDrivenStats[slot - 1u].FrameStamp = stamp;
    }

    drawArgs[slot].vertexCount = totalTri * 3;
    drawArgs[slot].instanceCount = 1;
    drawArgs[slot].firstVertex = 0;
    drawArgs[slot].firstInstance = 0;

    dispatchArgs[slot].x = itemCount;
    dispatchArgs[slot].y = 1;
    dispatchArgs[slot].z = 1;
}
