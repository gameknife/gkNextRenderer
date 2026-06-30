import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 Gid : SV_GroupID, uint3 Lid : SV_GroupThreadID)
{
    let scene = Bindless.GetGpuscene();
    uint slot = min(scene.custom_data_0, 4u);

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint* prims = slot == 0 ? (uint*)softMeshShader.Prim : (uint*)softMeshShader.ShadowPrim;

    SoftMeshShaderVisibleItem item = visibleItems[slot * MAX_NODES + Gid.x];
    uint encBase = (item.instanceIdx & 0x7FFF) << 17;
    for (uint t = Lid.x; t < item.triCount; t += 64)
    {
        prims[item.primBase + t] = encBase | (t & 0x1FFFF);
    }
}
