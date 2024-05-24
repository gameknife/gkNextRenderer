#pragma once

#include "Material.hpp"
#include "Vertex.hpp"
#include "UniformBuffer.hpp"
#include <vector>

namespace Assets
{

	class CornellBox final
	{
	public:

		static void Create(
			float scale,
			std::vector<Vertex>& vertices,
			std::vector<uint32_t>& indices,
			std::vector<Material>& materials,
			std::vector<LightObject>& lights);
	};

}
