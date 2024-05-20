#include "Texture.hpp"
#include "Utilities/StbImage.hpp"
#include "Utilities/Exception.hpp"
#include <chrono>
#include <iostream>

namespace Assets {

Texture Texture::LoadTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig)
{
	std::cout << "- loading '" << filename << "'... " << std::flush;
	const auto timer = std::chrono::high_resolution_clock::now();

	// Load the texture in normal host memory.
	int width, height, channels;
	const auto pixels = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);

	if (!pixels)
	{
		Throw(std::runtime_error("failed to load texture image '" + filename + "'"));
	}

	const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
	std::cout << "(" << width << " x " << height << " x " << channels << ") ";
	std::cout << elapsed << "s" << std::endl;

	return Texture(filename, width, height, channels, 0, pixels);
}

Texture Texture::LoadHDRTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig)
{
	std::cout << "- loading hdr '" << filename << "'... " << std::flush;
	const auto timer = std::chrono::high_resolution_clock::now();

	// Load the texture in normal host memory.
	int width, height, channels;
	void* pixels = stbi_loadf(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);

	if (!pixels)
	{
		Throw(std::runtime_error("failed to load texture image '" + filename + "'"));
	}

	const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
	std::cout << "(" << width << " x " << height << " x " << channels << ") ";
	std::cout << elapsed << "s" << std::endl;

	return Texture(filename, width, height, channels, 1, static_cast<unsigned char*>(pixels));
}

Texture::Texture(std::string loadname, int width, int height, int channels, int hdr, unsigned char* const pixels) :
	loadname_(loadname),
	width_(width),
	height_(height),
	channels_(channels),
	hdr_(hdr),
	pixels_(pixels, stbi_image_free)
{
}
	
}
