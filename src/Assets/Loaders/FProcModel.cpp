#include "Assets/Loaders/FProcModel.h"
#include "Assets/Data/Material.hpp"

#define _USE_MATH_DEFINES
#include <math.h>

namespace Assets
{
    void AddTriangle(std::vector<uint32_t>& indices, const uint32_t offset, const uint32_t i0, const uint32_t i1, const uint32_t i2)
    {
        indices.push_back(offset + i0);
        indices.push_back(offset + i1);
        indices.push_back(offset + i2);
    }
	
	uint32_t FProcModel::CreateCornellBox(const float scale,
	                                 std::vector<Model>& models,
	                                 std::vector<FMaterial>& materials,
	                                 std::vector<LightObject>& lights)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        uint32_t prevMatId = static_cast<uint32_t>(materials.size());
	
		materials.push_back({Material::Lambertian(vec3(0.65f, 0.05f, 0.05f))}); // red
		materials.push_back({Material::Lambertian(vec3(0.12f, 0.45f, 0.15f))}); // green
		materials.push_back({Material::Lambertian(vec3(0.73f, 0.73f, 0.73f))}); // white
		materials.push_back({Material::DiffuseLight(vec3(2000.0f))}); // light

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
		vertices.push_back(Vertex{ l0 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(0, 1), 1 });
		vertices.push_back(Vertex{ l1 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(1, 1), 1 });
		vertices.push_back(Vertex{ l2 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(1, 0), 1 });
		vertices.push_back(Vertex{ l3 - offset, vec3(1, 0, 0), vec4(1,0,0,0), vec2(0, 0), 1 });

		AddTriangle(indices, i, 0, 1, 2);
		AddTriangle(indices, i, 0, 2, 3);

		// Right red panel
		i = static_cast<uint32_t>(vertices.size());
		vertices.push_back(Vertex{ r0 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0, 1), 0 });
		vertices.push_back(Vertex{ r1 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(1, 1), 0 });
		vertices.push_back(Vertex{ r2 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(1, 0), 0 });
		vertices.push_back(Vertex{ r3 - offset, vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0, 0), 0 });

		AddTriangle(indices, i, 2, 1, 0);
		AddTriangle(indices, i, 3, 2, 0);

		// Back white panel
		i = static_cast<uint32_t>(vertices.size());
		vertices.push_back(Vertex{ l1 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(0, 1), 2 });
		vertices.push_back(Vertex{ r1 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(1, 1), 2 });
		vertices.push_back(Vertex{ r2 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(1, 0), 2 });
		vertices.push_back(Vertex{ l2 - offset, vec3(0, 0, 1), vec4(1,0,0,0), vec2(0, 0), 2 });

		AddTriangle(indices, i, 0, 1, 2);
		AddTriangle(indices, i, 0, 2, 3);

		// Bottom white panel
		i = static_cast<uint32_t>(vertices.size());
		vertices.push_back(Vertex{ l0 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(0, 1), 2 });
		vertices.push_back(Vertex{ r0 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(1, 1), 2 });
		vertices.push_back(Vertex{ r1 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(1, 0), 2 });
		vertices.push_back(Vertex{ l1 - offset, vec3(0, 1, 0), vec4(1,0,0,0), vec2(0, 0), 2 });

		AddTriangle(indices, i, 0, 1, 2);
		AddTriangle(indices, i, 0, 2, 3);

		// Top white panel
		i = static_cast<uint32_t>(vertices.size());
		vertices.push_back(Vertex{ l2 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 1), 2 });
		vertices.push_back(Vertex{ r2 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 1), 2 });
		vertices.push_back(Vertex{ r3 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 0), 2 });
		vertices.push_back(Vertex{ l3 - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 0), 2 });

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
		
			vertices.push_back(Vertex{ vec3(x0, y1, z1) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 1), 3 });
			vertices.push_back(Vertex{ vec3(x1, y1, z1) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 1), 3 });
			vertices.push_back(Vertex{ vec3(x1, y1, z0) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(1, 0), 3 });
			vertices.push_back(Vertex{ vec3(x0, y1, z0) - offset, vec3(0, -1, 0), vec4(1,0,0,0), vec2(0, 0), 3 });
		
			AddTriangle(indices, i, 0, 1, 2);
			AddTriangle(indices, i, 0, 2, 3);
		
			LightObject light {};
			light.p0 = vec4(vec3(x0, y1, z1) - offset, 1);
			light.p1 = vec4(vec3(x0, y1, z0) - offset, 1);
			light.p3 = vec4(vec3(x1, y1, z1) - offset, 1);
			light.normal_area = vec4(0, -1, 0, (x1 - x0) * (z0 - z1));
			light.lightMatIdx = prevMatId + 3;
			lights.push_back(light);
		}

        models.push_back(Model("cornell_box",
            std::move(vertices),
            std::move(indices),
            true
        ));

        return uint32_t(models.size() - 1);
    }

    Model FProcModel::CreateBox(const vec3& p0, const vec3& p1)
    {
        std::vector<Vertex> vertices =
        {
            Vertex{vec3(p0.x, p0.y, p0.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p0.y, p1.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p1.x, p0.y, p1.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p0.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p0.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p1.x, p0.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p0.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p0.x, p0.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p0.x, p0.y, p0.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p0.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p1.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p0.y, p1.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p1.x, p1.y, p0.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
        };

        std::vector<uint32_t> indices =
        {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23
        };
        
        return Model("pbox", std::move(vertices),std::move(indices), true);
    }

    Model FProcModel::CreateSphere(const vec3& center, float radius)
    {
        const int slices = 32;
        const int stacks = 16;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const float j0Delta = float(M_PI) / static_cast<float>(stacks);
        float j0 = 0.f;

        const float i0Delta = (float(M_PI) + float(M_PI)) / static_cast<float>(slices);
        float i0;

        for (int j = 0; j <= stacks; ++j)
        {
            // Vertex
            const float v = radius * -std::sin(j0);
            const float z = radius * std::cos(j0);

            // Normals		
            const float n0 = -std::sin(j0);
            const float n1 = std::cos(j0);

            i0 = 0;

            for (int i = 0; i <= slices; ++i)
            {
                const vec3 position(
                    center.x + v * std::sin(i0),
                    center.y + z,
                    center.z + v * std::cos(i0));

                const vec3 normal(
                    n0 * std::sin(i0),
                    n1,
                    n0 * std::cos(i0));

                const vec2 texCoord(
                    static_cast<float>(i) / slices,
                    static_cast<float>(j) / stacks);

                vertices.push_back(Vertex{position, normal, vec4(1,0,0,0), texCoord, 0});

                i0 += i0Delta;
            }

            j0 += j0Delta;
        }

        {
            int slices1 = slices + 1;
            int j0 = 0;
            int j1 = slices1;
            for (int j = 0; j < stacks; ++j)
            {
                for (int i = 0; i < slices; ++i)
                {
                    const auto i0 = i;
                    const auto i1 = i + 1;

                    indices.push_back(j0 + i0);
                    indices.push_back(j1 + i0);
                    indices.push_back(j1 + i1);

                    indices.push_back(j0 + i0);
                    indices.push_back(j1 + i1);
                    indices.push_back(j0 + i1);
                }

                j0 += slices1;
                j1 += slices1;
            }
        }

        return Model("sphere", std::move(vertices), std::move(indices));
    }
}
