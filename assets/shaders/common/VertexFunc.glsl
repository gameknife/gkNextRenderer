Vertex get_material_data(ivec2 pixel, uvec2 vBuffer, vec3 ray_origin, vec3 ray_direction)
{
    // fetch primitive_index high 14bit as instance index and low 18bit as triangle index
    NodeProxy proxy = NodeProxies[vBuffer.x - 1];

    Vertex result;
    vec3 positions[3], normals[3];
    vec2 tex_coords[3];
    uint matid;
    uint vertex_index = vBuffer.y * 3;

    for (int i = 0; i != 3; ++i) {
    	const Vertex v = UnpackVertex(vertex_index);
    	positions[i] = (proxy.worldTS * vec4(v.Position, 1)).xyz;
    	normals[i] = (proxy.worldTS * vec4(v.Normal, 0)).xyz;
    	tex_coords[i] = v.TexCoord;
    	if(i == 0) matid = v.MaterialIndex;
    	vertex_index++;
    }

    vec3 barycentrics;
    vec3 edges[2] = {
    	positions[1] - positions[0],
    	positions[2] - positions[0]
    };

    vec3 ray_cross_edge_1 = cross(ray_direction, edges[1]);
    float rcp_det_edges_direction = 1.0f / dot(edges[0], ray_cross_edge_1);
    vec3 ray_to_0 = ray_origin - positions[0];
    float det_0_dir_edge_1 = dot(ray_to_0, ray_cross_edge_1);
    barycentrics.y = rcp_det_edges_direction * det_0_dir_edge_1;
    vec3 edge_0_cross_0 = cross(edges[0], ray_to_0);
    float det_dir_edge_0_0 = dot(ray_direction, edge_0_cross_0);
    barycentrics.z = -rcp_det_edges_direction * det_dir_edge_0_0;
    barycentrics.x = 1.0f - (barycentrics.y + barycentrics.z);

    result.Position = fma(vec3(barycentrics[0]), positions[0], fma(vec3(barycentrics[1]), positions[1], barycentrics[2] * positions[2]));
    result.Normal = normalize(fma(vec3(barycentrics[0]), normals[0], fma(vec3(barycentrics[1]), normals[1], barycentrics[2] * normals[2])));
    result.TexCoord = fma(vec2(barycentrics[0]), tex_coords[0], fma(vec2(barycentrics[1]), tex_coords[1], barycentrics[2] * tex_coords[2]));
    result.MaterialIndex = matid;

    return result;
}