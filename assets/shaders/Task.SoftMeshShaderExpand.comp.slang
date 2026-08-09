import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.VisibilityBuffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 Gid : SV_GroupID, uint3 Lid : SV_GroupThreadID)
{
    let scene = Bindless.GetGpuscene();
    uint slot = min(scene.CustomData0, 4u);

    SoftMeshShaderResources softMeshShader = scene.SoftMeshShader[0];
    SoftMeshShaderVisibleItem* visibleItems = (SoftMeshShaderVisibleItem*)softMeshShader.VisibleItems;
    uint64_t primitiveAddress = slot == 0 ? softMeshShader.Prim : softMeshShader.ShadowPrim;

    SoftMeshShaderVisibleItem item = visibleItems[slot * MAX_RENDER_PROXIES + Gid.x];
    for (uint t = Lid.x; t < item.triCount; t += 64)
    {
        Visibility.StoreExpandedPrimitive(
            primitiveAddress, item.primBase + t, Visibility.MakeId(item.instanceIdx, t));
    }
}
