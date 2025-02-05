#include "CornellBox.hpp"

#include "UniformBuffer.hpp"
using namespace glm;

namespace Assets {

namespace
{
	void AddTriangle(std::vector<uint32_t>& indices, const uint32_t offset, const uint32_t i0, const uint32_t i1, const uint32_t i2)
	{
		indices.push_back(offset + i0);
		indices.push_back(offset + i1);
		indices.push_back(offset + i2);
	}
}

void CornellBox::Create(
	const float scale,
	std::vector<Vertex>& vertices,
	std::vector<uint32_t>& indices,
	std::vector<FMaterial>& materials,
	std::vector<LightObject>& lights)
{
	uint32_t prev_mat_id = static_cast<uint32_t>(materials.size());
	
	materials.push_back({"", 0,Material::Lambertian(vec3(0.65f, 0.05f, 0.05f))}); // red
	materials.push_back({"", 1,Material::Lambertian(vec3(0.12f, 0.45f, 0.15f))}); // green
	materials.push_back({"", 2,Material::Lambertian(vec3(0.73f, 0.73f, 0.73f))}); // white
	materials.push_back({"", 3,Material::DiffuseLight(vec3(2000.0f))}); // light

	const float s = scale;

	const vec3 offset(s*0.5, 0, -s*0.5);

	const vec3 l0(0, 0, 0);
	const vec3 l1(0, 0, -s);
	const vec3 l2(0, s, -s);
	const vec3 l3(0, s, 0);

	const vec3 r0(s, 0, 0);
	const vec3 r1(s, 0, -s);
	const vec3 r2(s, s, -s);
	const vec3 r3(s, s, 0);

	// Left green panel
	uint32_t i = static_cast<uint32_t>(vertices.size());
	vertices.push_back(Vertex{ l0 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(0, 1), prev_mat_id + 1 });
	vertices.push_back(Vertex{ l1 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(1, 1), prev_mat_id + 1 });
	vertices.push_back(Vertex{ l2 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(1, 0), prev_mat_id + 1 });
	vertices.push_back(Vertex{ l3 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(0, 0), prev_mat_id + 1 });

	AddTriangle(indices, i, 0, 1, 2);
	AddTriangle(indices, i, 0, 2, 3);

	// Right red panel
	i = static_cast<uint32_t>(vertices.size());
	vertices.push_back(Vertex{ r0 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0, 1), prev_mat_id + 0 });
	vertices.push_back(Vertex{ r1 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(1, 1), prev_mat_id + 0 });
	vertices.push_back(Vertex{ r2 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(1, 0), prev_mat_id + 0 });
	vertices.push_back(Vertex{ r3 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0, 0), prev_mat_id + 0 });

	AddTriangle(indices, i, 2, 1, 0);
	AddTriangle(indices, i, 3, 2, 0);

	// Back white panel
	i = static_cast<uint32_t>(vertices.size());
	vertices.push_back(Vertex{ l1 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(0, 1), prev_mat_id + 2 });
	vertices.push_back(Vertex{ r1 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(1, 1), prev_mat_id + 2 });
	vertices.push_back(Vertex{ r2 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(1, 0), prev_mat_id + 2 });
	vertices.push_back(Vertex{ l2 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(0, 0), prev_mat_id + 2 });

	AddTriangle(indices, i, 0, 1, 2);
	AddTriangle(indices, i, 0, 2, 3);

	// Bottom white panel
	i = static_cast<uint32_t>(vertices.size());
	vertices.push_back(Vertex{ l0 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(0, 1), prev_mat_id + 2 });
	vertices.push_back(Vertex{ r0 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(1, 1), prev_mat_id + 2 });
	vertices.push_back(Vertex{ r1 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(1, 0), prev_mat_id + 2 });
	vertices.push_back(Vertex{ l1 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(0, 0), prev_mat_id + 2 });

	AddTriangle(indices, i, 0, 1, 2);
	AddTriangle(indices, i, 0, 2, 3);

	// Top white panel
	i = static_cast<uint32_t>(vertices.size());
	vertices.push_back(Vertex{ l2 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 1), prev_mat_id + 2 });
	vertices.push_back(Vertex{ r2 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 1), prev_mat_id + 2 });
	vertices.push_back(Vertex{ r3 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 0), prev_mat_id + 2 });
	vertices.push_back(Vertex{ l3 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 0), prev_mat_id + 2 });

	AddTriangle(indices, i, 0, 1, 2);
	AddTriangle(indices, i, 0, 2, 3);

	// Light
	i = static_cast<uint32_t>(vertices.size());
	{
		const float x0 = s * (163.0f / 555.0f);
		const float x1 = s * (393.0f / 555.0f);
		const float z0 = s * (-555.0f + 432.0f) / 555.0f;
		const float z1 = s * (-555.0f + 202.0f) / 555.0f;
		const float y1 = s * 0.999f;
	
		vertices.push_back(Vertex{ vec3(x0, y1, z1) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 1), prev_mat_id + 3 });
		vertices.push_back(Vertex{ vec3(x1, y1, z1) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 1), prev_mat_id + 3 });
		vertices.push_back(Vertex{ vec3(x1, y1, z0) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 0), prev_mat_id + 3 });
		vertices.push_back(Vertex{ vec3(x0, y1, z0) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 0), prev_mat_id + 3 });
	
		AddTriangle(indices, i, 0, 1, 2);
		AddTriangle(indices, i, 0, 2, 3);
	
		LightObject light {};
		light.p0 = vec4(vec3(x0, y1, z1) - offset, 1);
		light.p1 = vec4(vec3(x0, y1, z0) - offset, 1);
		light.p3 = vec4(vec3(x1, y1, z1) - offset, 1);
		light.normal_area = vec4(0, -1, 0, (x1 - x0) * (z0 - z1));
		lights.push_back(light);
	}

	
	// Second Light
	// i = static_cast<uint32_t>(vertices.size());
	// {
	// 	const float x0 = s * (13.0f / 555.0f);
	// 	const float x1 = s * (143.0f / 555.0f);
	// 	const float z0 = s * (-555.0f + 442.0f) / 555.0f;
	// 	const float z1 = s * (-555.0f + 312.0f) / 555.0f;
	// 	const float y1 = s * 0.001f;
	// 	
	// 	materials.push_back(Material::DiffuseLight(vec3(2.0f))); // light
	//
	// 	vertices.push_back(Vertex{ vec3(x0, y1, z1), vec3(0, 1, 0), vec2(0, 1), 4 });
	// 	vertices.push_back(Vertex{ vec3(x1, y1, z1), vec3(0, 1, 0), vec2(1, 1), 4 });
	// 	vertices.push_back(Vertex{ vec3(x1, y1, z0), vec3(0, 1, 0), vec2(1, 0), 4 });
	// 	vertices.push_back(Vertex{ vec3(x0, y1, z0), vec3(0, 1, 0), vec2(0, 0), 4 });
	//
	// 	AddTriangle(indices, i, 0, 2, 1);
	// 	AddTriangle(indices, i, 0, 3, 2);
	//
	// 	LightObject light {};
	// 	light.p0 = vec4(vec3(x0, y1, z1) - offset, 1);
	// 	light.p1 = vec4(vec3(x0, y1, z0) - offset, 1);
	// 	light.p3 = vec4(vec3(x1, y1, z1) - offset, 1);
	// 	light.normal_area = vec4(0, -1, 0, (x1 - x0) * (z0 - z1));
	//
	// 	lights.push_back(light);
	// }
	
}

}
