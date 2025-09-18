import Common;
import Bindless;

static const int WORK_GROUP_AXIS_SIZE = 8;
static const int WINDOW_R = 5; // HALF SIZE, must be even, 8 is a 17 x 17 sobel
static const int WINDOW_LEN = WORK_GROUP_AXIS_SIZE + 2 * WINDOW_R;

// Shared memory arrays
groupshared float3 diffuse[WINDOW_LEN][WINDOW_LEN];
groupshared float3 spec[WINDOW_LEN][WINDOW_LEN];
groupshared float3 normal[WINDOW_LEN][WINDOW_LEN];
groupshared uint visibility[WINDOW_LEN][WINDOW_LEN];

float3 FetchDiffuse(int2 ipos)
{
    return diffuse[ipos.x][ipos.y] + float3(0.001, 0.001, 0.001);
}

float3 FetchSpec(int2 ipos)
{
    return spec[ipos.x][ipos.y] + float3(0.001, 0.001, 0.001);
}

float3 FetchNormal(int2 ipos)
{
    return normal[ipos.x][ipos.y];
}

uint FetchVisibility(int2 ipos)
{
    return visibility[ipos.x][ipos.y];
}

bool EdgeDetect(uint current_primitive_index, RWTexture2D<uint> ObjectImage, int2 ipos)
{
    uint center = current_primitive_index;

    uint prev_primitive_index0 = (ObjectImage[ipos + int2(1, 1)]);
    uint prev_primitive_index1 = (ObjectImage[ipos + int2(-1, -1)]);
    uint prev_primitive_index2 = (ObjectImage[ipos + int2(-1, 1)]);
    uint prev_primitive_index3 = (ObjectImage[ipos + int2(1, -1)]);

    bool edge0 = any(uint4(prev_primitive_index0, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) != uint4(center));
    bool edge1 = any(uint4(prev_primitive_index0, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) == uint4(center));

    return edge0 && edge1;
}

void JBF(int2 coord, int2 localCoord, inout float3 Total, inout float Weight, float CenterLum, float3 CenterNormal, uint CenterVisibility)
{
    const float sigma = Bindless.GetGpuscene().Camera->BFSigma; // spatial sigma
    const float sigmaL = Bindless.GetGpuscene().Camera->BFSigmaLum * 100.0; // 
    const float sigmaN = Bindless.GetGpuscene().Camera->BFSigmaNormal;
    const int N = int(WINDOW_R); // filter size
    
    const float3 FinalCol = FetchDiffuse(coord);
    const float3 Ci = FinalCol;
    const float3 Ni = FetchNormal(coord);
    const uint Pi = FetchVisibility(coord);
    const float lumi = dot(Ci, float3(0.212671f, 0.715160f, 0.072169f));

    const float dist = clamp(float(localCoord.x * localCoord.x + localCoord.y * localCoord.y) / float(N * N), 0.0, 1.0);
    const float normaldist = 1.0 - dot(Ni, CenterNormal);
    const float dlum = (CenterLum - lumi) * (CenterLum - lumi);

    const float Fi = exp(-dist * dist / (2.0 * sigma * sigma));
    const float Ai = exp(-normaldist * normaldist / (2.0 * sigmaN * sigmaN));
    const float Li = exp(-dlum * dlum / (2.0 * sigmaL * sigmaL));
    const float Oi = float(CenterVisibility == Pi);

    Total += Ci * Fi * Li; // * Ai; // * Oi;
    Weight += Fi * Li;// * Ai;// * Oi;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId: SV_DispatchThreadID, 
                 uint3 groupThreadId: SV_GroupThreadID,
                 uint3 groupId: SV_GroupID,
                 uint groupIndex: SV_GroupIndex)
{
    UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

    let OutImage = Bindless.GetStorageTexture<float4>(Bindless.RT_DENOISED);
    let FinalImage = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_DIFFUSE);
    let FinalImageSpec = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_SPECULAR);
    let AccumlateAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_ALBEDO);
    let OutNormalBuffer = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL);
    let ObjectId0 = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);
    let ObjectId1 = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_1);

    const int2 globalOffset = int2(Camera.ViewportRect.xy);
    const int2 ipos = int2(dispatchThreadId.xy) + globalOffset;
    bool isEvenFrame = false; // Camera.TotalFrames % 2 == 0 ? true : false;

    float3 Total = float3(0, 0, 0);
    
    if(Camera.BFSize > 0)
    {
        // share memory
        int row = int(groupIndex / WINDOW_LEN);
        int col = int(groupIndex % WINDOW_LEN);
        int2 coord = int2(groupId.xy) * WORK_GROUP_AXIS_SIZE + int2(col - WINDOW_R, row - WINDOW_R) + globalOffset;
        diffuse[row][col] = FinalImage[coord].rgb;
        spec[row][col] = FinalImageSpec[coord].rgb;
        normal[row][col] = OutNormalBuffer[coord].rgb;
        visibility[row][col] = ObjectId0[coord].r;
        
        int idx = int(groupIndex + WORK_GROUP_AXIS_SIZE * WORK_GROUP_AXIS_SIZE);
        while (idx < WINDOW_LEN * WINDOW_LEN)
        {
            row = idx / WINDOW_LEN;
            col = idx % WINDOW_LEN;
            coord = int2(groupId.xy) * WORK_GROUP_AXIS_SIZE + int2(col - WINDOW_R, row - WINDOW_R) + globalOffset;
            diffuse[row][col] = FinalImage[coord].rgb;
            spec[row][col] = FinalImageSpec[coord].rgb;
            normal[row][col] = OutNormalBuffer[coord].rgb;
            visibility[row][col] = ObjectId0[coord].r;

            idx += WORK_GROUP_AXIS_SIZE * WORK_GROUP_AXIS_SIZE;
        }
        GroupMemoryBarrierWithGroupSync();
        // now, the WINDOW_R + WINDOW_R range pixel is in the shared memory
        // free to access

        coord = int2(groupThreadId.yx) + WINDOW_R;

        const float3 CenterColor = FetchDiffuse(coord.xy).rgb;
        const float3 CenterSpec = FetchSpec(coord.xy).rgb;
        const float3 Normal = FetchNormal(coord.xy).rgb;
        uint current_primitive_index = FetchVisibility(coord.xy);

        const float CenterLum = dot(CenterColor, float3(0.212671f, 0.715160f, 0.072169f));

        float Weight = 0;
        for (int i = -WINDOW_R; i <= WINDOW_R; i += 2)
        {
            for (int j = -WINDOW_R; j <= WINDOW_R; j += 2)
            {
                JBF(coord.xy + int2(i, j), int2(i, j), Total, Weight, CenterLum, Normal, current_primitive_index);
            }
        }
        Total /= Weight;

        if (!Camera.DebugDraw_Lighting)
        {
            Total = Total * AccumlateAlbedo[ipos].rgb + CenterSpec;
        }
    }
    else
    {
        if (Camera.DebugDraw_Lighting)
        {
            Total = FinalImage[ipos].rgb * float3(0.5, 0.5, 0.5) + FinalImageSpec[ipos].rgb;
        }
        else
        {
            Total = FinalImage[ipos].rgb * AccumlateAlbedo[ipos].rgb + FinalImageSpec[ipos].rgb;
        }
    }

    // float edgeThis = EdgeDetect(Camera.SelectedId, ObjectId0, ipos) ? 0.5 : 0.0;
    // float edgePrev = EdgeDetect(Camera.SelectedId, ObjectId1, ipos) ? 0.5 : 0.0;
    // // selected edge
    // if (edgeThis + edgeThis > 0)
    // {
    //     Total = lerp(Total, float3(150, 100, 0), edgeThis + edgePrev);
    // }
    
    if(Camera.HDR)
    {
        Total = Total / 2000;
        Total = GT_Tonemapping(Total);
        Total = Total * 2000;
        float3 st2084 = LinearToST2084UE(Total * Camera.PaperWhiteNit / 230.0);
        OutImage[ipos] = float4(st2084, 1.0);
    }
    else
    {
        OutImage[ipos] = float4(GT_Tonemapping(Total * Camera.PaperWhiteNit / 40000.0), 1.0);
    }


}