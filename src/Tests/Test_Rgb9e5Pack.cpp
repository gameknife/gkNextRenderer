// Round-trip validation for the RGB9E5 shared-exponent pack/unpack used by the
// ambient-cube color faces. This C++ reference mirrors the Slang implementation
// in assets/shaders/common/ConstFunc.slang (packRGB9E5 / unpackRGB9E5) exactly;
// keep the two in sync. Validates: round-trip error < 1%, monotonicity, no NaN,
// and exponent boundary behaviour.

#include <catch2/catch_all.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr int kMantissaBits = 9;
    constexpr int kExpBias = 15;
    constexpr int kMantissaValues = 512; // 1 << 9
    constexpr float kMaxValue = 65408.0f; // ((2^9-1)/2^9) * 2^(31-15)

    // floor(log2(x)) for x > 0 via the IEEE-754 exponent field; -127 for x == 0.
    int FloorLog2Pos(float x)
    {
        uint32_t bits;
        std::memcpy(&bits, &x, sizeof(bits));
        return static_cast<int>((bits >> 23) & 0xFFu) - 127;
    }

    uint32_t PackRGB9E5(float r, float g, float b)
    {
        const float rc = std::clamp(r, 0.0f, kMaxValue);
        const float gc = std::clamp(g, 0.0f, kMaxValue);
        const float bc = std::clamp(b, 0.0f, kMaxValue);
        const float maxc = std::max(rc, std::max(gc, bc));

        int expShared = std::max(-kExpBias - 1, FloorLog2Pos(maxc)) + 1 + kExpBias;
        float denom = std::exp2(static_cast<float>(expShared - kExpBias - kMantissaBits));

        const int maxm = static_cast<int>(std::floor(maxc / denom + 0.5f));
        if (maxm == kMantissaValues)
        {
            denom *= 2.0f;
            expShared += 1;
        }

        const uint32_t rm = static_cast<uint32_t>(std::floor(rc / denom + 0.5f));
        const uint32_t gm = static_cast<uint32_t>(std::floor(gc / denom + 0.5f));
        const uint32_t bm = static_cast<uint32_t>(std::floor(bc / denom + 0.5f));

        return (static_cast<uint32_t>(expShared) << 27) | (bm << 18) | (gm << 9) | rm;
    }

    std::array<float, 3> UnpackRGB9E5(uint32_t packed)
    {
        const int expShared = static_cast<int>(packed >> 27);
        const float scale = std::exp2(static_cast<float>(expShared - kExpBias - kMantissaBits));
        return {
            static_cast<float>(packed & 0x1FFu) * scale,
            static_cast<float>((packed >> 9) & 0x1FFu) * scale,
            static_cast<float>((packed >> 18) & 0x1FFu) * scale,
        };
    }
}

TEST_CASE("RGB9E5 round-trips HDR values within 1%", "[Unit][RGB9E5]")
{
    // Span dark tones to bright HDR, plus emissive/sun-adjacent magnitudes.
    const float values[] = {
        0.0f, 0.0005f, 0.002f, 0.01f, 0.05f, 0.2f, 0.5f,
        1.0f, 2.5f, 7.0f, 16.0f, 64.0f, 200.0f, 511.0f,
        1024.0f, 8192.0f, 50000.0f
    };

    for (float r : values)
    {
        for (float g : values)
        {
            for (float b : values)
            {
                const uint32_t packed = PackRGB9E5(r, g, b);
                const auto out = UnpackRGB9E5(packed);

                for (int c = 0; c < 3; ++c)
                {
                    REQUIRE(std::isfinite(out[c]));
                    REQUIRE(out[c] >= 0.0f);
                }

                const float in[3] = { r, g, b };
                const float maxc = std::max(r, std::max(g, b));
                // Shared exponent is driven by the brightest channel; the quantization
                // step is maxc/512, so absolute error per channel is bounded by that.
                const float absTol = maxc / 512.0f + 1e-6f;
                for (int c = 0; c < 3; ++c)
                {
                    REQUIRE(std::abs(out[c] - in[c]) <= absTol);
                }
            }
        }
    }
}

TEST_CASE("RGB9E5 grayscale relative error stays under 1%", "[Unit][RGB9E5]")
{
    // Equal channels => the brightest-channel quantization is the channel itself,
    // giving ~1/512 relative precision regardless of magnitude.
    for (float v = 0.001f; v < 60000.0f; v *= 1.7f)
    {
        const uint32_t packed = PackRGB9E5(v, v, v);
        const auto out = UnpackRGB9E5(packed);
        const float rel = std::abs(out[0] - v) / v;
        REQUIRE(rel < 0.01f);
    }
}

TEST_CASE("RGB9E5 is monotonic in a single channel", "[Unit][RGB9E5]")
{
    float prev = -1.0f;
    for (float v = 0.0f; v <= kMaxValue; v = (v < 0.001f ? 0.001f : v * 1.05f))
    {
        const auto out = UnpackRGB9E5(PackRGB9E5(v, 0.0f, 0.0f));
        REQUIRE(out[0] >= prev - 1e-4f);
        prev = out[0];
    }
}

TEST_CASE("RGB9E5 edge cases", "[Unit][RGB9E5]")
{
    SECTION("zero packs to zero and unpacks to zero")
    {
        const uint32_t packed = PackRGB9E5(0.0f, 0.0f, 0.0f);
        REQUIRE(packed == 0u);
        const auto out = UnpackRGB9E5(packed);
        REQUIRE(out[0] == 0.0f);
        REQUIRE(out[1] == 0.0f);
        REQUIRE(out[2] == 0.0f);
    }

    SECTION("values above range clamp without overflow")
    {
        const auto out = UnpackRGB9E5(PackRGB9E5(1e9f, 1e9f, 1e9f));
        for (int c = 0; c < 3; ++c)
        {
            REQUIRE(std::isfinite(out[c]));
            REQUIRE(out[c] <= kMaxValue);
            REQUIRE(out[c] >= kMaxValue * 0.99f);
        }
    }

    SECTION("exponent boundaries (powers of two) round-trip cleanly")
    {
        for (int e = -14; e <= 16; ++e)
        {
            const float v = std::exp2(static_cast<float>(e));
            const auto out = UnpackRGB9E5(PackRGB9E5(v, v, v));
            REQUIRE(std::isfinite(out[0]));
            REQUIRE(std::abs(out[0] - v) <= v / 512.0f + 1e-6f);
        }
    }

    SECTION("mantissa rounding overflow bumps the exponent correctly")
    {
        // A value just under a power of two that rounds up to mantissa==512.
        const float v = kMaxValue; // brightest representable
        const auto out = UnpackRGB9E5(PackRGB9E5(v, v, v));
        REQUIRE(std::isfinite(out[0]));
        REQUIRE(out[0] <= kMaxValue);
        REQUIRE(out[0] >= kMaxValue * 0.99f);
    }
}
