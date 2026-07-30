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
    itemCount = min(itemCount, softMeshShader.NodeCapacity);
    if (slot == 0)
    {
        scene.GPUDrivenStats[0].VisibleCount = itemCount;
    }
    else
    {
        scene.ShadowGPUDrivenStats[slot - 1].VisibleCount = itemCount;
    }

    drawArgs[slot].vertexCount = totalTri * 3;
    drawArgs[slot].instanceCount = 1;
    drawArgs[slot].firstVertex = 0;
    drawArgs[slot].firstInstance = 0;

    dispatchArgs[slot].x = itemCount;
    dispatchArgs[slot].y = 1;
    dispatchArgs[slot].z = 1;
}
