#include "Engine/Assets/GPU/HdrTextureCache.hpp"

#include <catch2/catch_all.hpp>

TEST_CASE("HDR SH projection matches equirectangular sampling axes", "[Unit][HDR][SH]")
{
    constexpr int width = 64;
    constexpr int height = 32;
    std::vector<float> pixels(width * height * 4, 0.0f);

    // SampleIBL maps u=0.25 at the equator to the +X direction.
    const int x = width / 4;
    const int y = height / 2;
    const size_t pixelIndex = static_cast<size_t>(y * width + x) * 4;
    pixels[pixelIndex] = 1.0f;
    pixels[pixelIndex + 3] = 1.0f;

    const Assets::SphericalHarmonics sh = Assets::ProjectHdrToSh(pixels.data(), width, height);

    CHECK(sh.coefficients[0][3] < 0.0f);
    CHECK(std::abs(sh.coefficients[0][3]) > std::abs(sh.coefficients[0][2]) * 10.0f);
}
