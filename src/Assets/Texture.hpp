#pragma once

#include "Vulkan/Sampler.hpp"
#include <memory>
#include <string>

namespace Assets
{
	class Texture final
	{
	public:

		static Texture LoadTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig);
		static Texture LoadHDRTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig);

		Texture& operator = (const Texture&) = delete;
		Texture& operator = (Texture&&) = delete;

		Texture() = default;
		Texture(const Texture&) = default;
		Texture(Texture&&) = default;
		~Texture() = default;

		const unsigned char* Pixels() const { return pixels_.get(); }
		int Width() const { return width_; }
		int Height() const { return height_; }
		bool Hdr() const {return hdr_ != 0; }
		const std::string& Loadname() const { return loadname_; }

	private:

		Texture(std::string loadname, int width, int height, int channels, int hdr, unsigned char* pixels);

		Vulkan::SamplerConfig samplerConfig_;
		std::string loadname_;
		int width_;
		int height_;
		int channels_;
		int hdr_;
		std::unique_ptr<unsigned char, void (*) (void*)> pixels_;
	};

}
