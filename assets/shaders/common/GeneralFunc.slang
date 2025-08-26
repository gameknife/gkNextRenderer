implementing Common;
#include "PreProcessor.slang"

namespace Common
{
    public Vertex get_material_dataV2(uint2 vBuffer, float3 ray_origin, float3 ray_direction, in NodeProxy* NodeProxies, in ModelData* Models, in GPUVertex* Vertices, in uint* PrimAddress)
    {
        // fetch primitive_index high 14bit as instance index and low 18bit as triangle index
        NodeProxy proxy = NodeProxies[vBuffer.x - 1];
        ModelData model = Models[proxy.modelId];

        Vertex result;
        float3 positions[3], normals[3], tangents[3], localNormals[3];
        float4 localTangents[3];
        float2 tex_coords[3];
        uint matid = 0;

        uint primitive_index = vBuffer.y; // triangle index in model, so has to add by modelinfo

        for (int i = 0; i != 3; ++i) {
            const Vertex v = UnpackVertexV2(Vertices[model.vertexOffset + PrimAddress[model.indexOffset + primitive_index * 3 + i]]);
            positions[i] = mul(proxy.worldTS, float4(v.Position, 1)).xyz;
            normals[i] = mul(proxy.worldTS, float4(v.Normal, 0)).xyz;
            tangents[i] = mul(proxy.worldTS, float4(v.Tangent.xyz, 0)).xyz;
            localNormals[i] = v.Normal;
            localTangents[i] = v.Tangent;
            tex_coords[i] = v.TexCoord;
            if(i == 0) matid = v.MaterialIndex;
        }

        float3 barycentrics;
        float3 edges[2] = {
            positions[1] - positions[0],
            positions[2] - positions[0]
        };

        float3 ray_cross_edge_1 = cross(ray_direction, edges[1]);
        float rcp_det_edges_direction = 1.0f / dot(edges[0], ray_cross_edge_1);
        float3 ray_to_0 = ray_origin - positions[0];
        float det_0_dir_edge_1 = dot(ray_to_0, ray_cross_edge_1);
        barycentrics.y = rcp_det_edges_direction * det_0_dir_edge_1;
        float3 edge_0_cross_0 = cross(edges[0], ray_to_0);
        float det_dir_edge_0_0 = dot(ray_direction, edge_0_cross_0);
        barycentrics.z = -rcp_det_edges_direction * det_dir_edge_0_0;
        barycentrics.x = 1.0f - (barycentrics.y + barycentrics.z);

        result.Position = mad(float3(barycentrics.x), positions[0], mad(float3(barycentrics.y), positions[1], barycentrics.z * positions[2]));
        result.Normal = normalize(mad(float3(barycentrics.x), normals[0], mad(float3(barycentrics.y), normals[1], barycentrics.z * normals[2])));
        result.Tangent.xyz = normalize(mad(float3(barycentrics.x), tangents[0], mad(float3(barycentrics.y), tangents[1], barycentrics.z * tangents[2])));
        result.TexCoord = mad(float2(barycentrics.x), tex_coords[0], mad(float2(barycentrics.y), tex_coords[1], barycentrics.z * tex_coords[2]));
        result.LocalNormal =  mad(float3(barycentrics.x), localNormals[0], mad(float3(barycentrics.y), localNormals[1], barycentrics.z * localNormals[2]));
        result.LocalTangent = mad(float4(barycentrics.x), localTangents[0], mad(float4(barycentrics.y), localTangents[1], barycentrics.z * localTangents[2]));
        result.MaterialIndex = matid;

        return result;
    }

    public Vertex get_material_data(uint2 vBuffer, float3 ray_origin, float3 ray_direction, in StructuredBuffer<NodeProxy> NodeProxies, StructuredBuffer<ModelData> Models, in StructuredBuffer<GPUVertex> Vertices, StructuredBuffer<uint> PrimAddress)
    {
        // fetch primitive_index high 14bit as instance index and low 18bit as triangle index
        NodeProxy proxy = NodeProxies[vBuffer.x - 1];
        ModelData model = Models[proxy.modelId];

        Vertex result;
        float3 positions[3], normals[3], tangents[3], localNormals[3];
        float4 localTangents[3];
        float2 tex_coords[3];
        uint matid = 0;

        uint primitive_index = vBuffer.y; // triangle index in model, so has to add by modelinfo

        for (int i = 0; i != 3; ++i) {
            const Vertex v = UnpackVertex(model.vertexOffset + PrimAddress[model.indexOffset + primitive_index * 3 + i], Vertices);
            positions[i] = mul(proxy.worldTS, float4(v.Position, 1)).xyz;
            normals[i] = mul(proxy.worldTS, float4(v.Normal, 0)).xyz;
            tangents[i] = mul(proxy.worldTS, float4(v.Tangent.xyz, 0)).xyz;
            localNormals[i] = v.Normal;
            localTangents[i] = v.Tangent;
            tex_coords[i] = v.TexCoord;
            if(i == 0) matid = v.MaterialIndex;
        }

        float3 barycentrics;
        float3 edges[2] = {
            positions[1] - positions[0],
            positions[2] - positions[0]
        };

        float3 ray_cross_edge_1 = cross(ray_direction, edges[1]);
        float rcp_det_edges_direction = 1.0f / dot(edges[0], ray_cross_edge_1);
        float3 ray_to_0 = ray_origin - positions[0];
        float det_0_dir_edge_1 = dot(ray_to_0, ray_cross_edge_1);
        barycentrics.y = rcp_det_edges_direction * det_0_dir_edge_1;
        float3 edge_0_cross_0 = cross(edges[0], ray_to_0);
        float det_dir_edge_0_0 = dot(ray_direction, edge_0_cross_0);
        barycentrics.z = -rcp_det_edges_direction * det_dir_edge_0_0;
        barycentrics.x = 1.0f - (barycentrics.y + barycentrics.z);

        result.Position = mad(float3(barycentrics.x), positions[0], mad(float3(barycentrics.y), positions[1], barycentrics.z * positions[2]));
        result.Normal = normalize(mad(float3(barycentrics.x), normals[0], mad(float3(barycentrics.y), normals[1], barycentrics.z * normals[2])));
        result.Tangent.xyz = normalize(mad(float3(barycentrics.x), tangents[0], mad(float3(barycentrics.y), tangents[1], barycentrics.z * tangents[2])));
        result.TexCoord = mad(float2(barycentrics.x), tex_coords[0], mad(float2(barycentrics.y), tex_coords[1], barycentrics.z * tex_coords[2]));
        result.LocalNormal =  mad(float3(barycentrics.x), localNormals[0], mad(float3(barycentrics.y), localNormals[1], barycentrics.z * localNormals[2]));
        result.LocalTangent = mad(float4(barycentrics.x), localTangents[0], mad(float4(barycentrics.y), localTangents[1], barycentrics.z * localTangents[2]));
        result.MaterialIndex = matid;

        return result;
    }
}