#include "Modules/NextDotNet/EngineApi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

// Keeps the three views of the binding surface in agreement: the def file, the FEngineApi struct
// it expands into, and the table that BuildEngineApi fills. The fourth view — the generated C# —
// is checked by `gnb csharpgen --check`, which compares the file on disk against a fresh
// generation from the same def file.

using namespace Modules::NextDotNet;

namespace
{
    /// The table is a version field followed by nothing but function pointers, so it can be walked
    /// as an array. This is what makes "every declared binding is implemented" checkable at all.
    std::vector<const void*> CollectEntryPointers(const FEngineApi& api)
    {
        constexpr size_t headerBytes = offsetof(FEngineApi, Log_Info);
        const size_t pointerBytes = sizeof(FEngineApi) - headerBytes;
        REQUIRE(pointerBytes % sizeof(void*) == 0);

        const auto* base = reinterpret_cast<const unsigned char*>(&api) + headerBytes;
        std::vector<const void*> pointers(pointerBytes / sizeof(void*));
        std::memcpy(pointers.data(), base, pointerBytes);
        return pointers;
    }
}

TEST_CASE("EngineApi table has one field per def entry", "[Unit][DotNet]")
{
    const FEngineApi api = BuildEngineApi();
    const std::vector<const void*> pointers = CollectEntryPointers(api);

    // A def entry that failed to produce a struct field, or a field added by hand without a def
    // entry, shows up here as a size mismatch.
    CHECK(pointers.size() == GK_ENGINE_API_COUNT);
}

TEST_CASE("every EngineApi binding is implemented", "[Unit][DotNet]")
{
    const FEngineApi api = BuildEngineApi();

    CHECK(api.Version == GK_DOTNET_ABI_VERSION);

    // A null entry means managed code would call through a null pointer and take the process down
    // with it. The table fill is expanded from the def file, so this cannot normally happen — the
    // test exists because the cost of it happening is a crash with no diagnostic.
    for (const void* pointer : CollectEntryPointers(api))
    {
        CHECK(pointer != nullptr);
    }
}

TEST_CASE("SceneBuild bindings refuse to run outside the rebuild hook", "[Unit][DotNet]")
{
    const FEngineApi api = BuildEngineApi();
    REQUIRE_FALSE(GSceneBuildContext.IsValid());

    // Outside the BeforeSceneRebuild window there is nothing to build into. The QuickJS bindings
    // threw a JS exception here; across this ABI the contract is to return an invalid id rather
    // than write through a stale pointer.
    const FVec3 min{-1.0f, -1.0f, -1.0f};
    const FVec3 max{1.0f, 1.0f, 1.0f};
    CHECK(api.SceneBuild_AddBoxModel(&min, &max) == 0);
    CHECK(api.SceneBuild_AddSphereModel(&min, 1.0f) == 0);
    CHECK(api.SceneBuild_AddLambertianMaterial(&max) == 0);
}

TEST_CASE("string bindings report the length a caller must allocate", "[Unit][DotNet]")
{
    const FEngineApi api = BuildEngineApi();

    // The probe convention: a null buffer asks for the size, and it must agree with what a real
    // call writes. Generated C# depends on this for every string-returning binding.
    const int32_t required = api.Paths_GetProjectRoot(nullptr, 0);
    REQUIRE(required > 0);

    std::vector<char> buffer(static_cast<size_t>(required));
    const int32_t written = api.Paths_GetProjectRoot(buffer.data(), required);
    CHECK(written == required);

    // A short buffer truncates instead of overflowing.
    std::vector<char> shortBuffer(4, '\0');
    const int32_t truncated = api.Paths_GetProjectRoot(shortBuffer.data(), 4);
    CHECK(truncated == 4);
}

TEST_CASE("input bindings are safe without an engine", "[Unit][DotNet]")
{
    const FEngineApi api = BuildEngineApi();
    GInputState.Reset();

    const GkStr empty{nullptr, 0};
    CHECK(api.Input_IsKeyDown(empty) == 0);
    CHECK(api.Input_IsKeyPressed(empty) == 0);

    // The "any key" form the Flappy replay depends on: an empty name means "is anything down".
    GInputState.keysDown.insert(SDLK_SPACE);
    CHECK(api.Input_IsKeyDown(empty) == 1);

    const char* spaceName = "space";
    const GkStr space{spaceName, 5};
    CHECK(api.Input_IsKeyDown(space) == 1);

    GInputState.Reset();
    CHECK(api.Input_IsKeyDown(space) == 0);
}

TEST_CASE("managed pressed input survives only its engine frame", "[Unit][DotNet]")
{
    GInputState.Reset();
    GInputState.BeginPressedFrame(41);
    GInputState.keysPressed.insert(SDLK_RETURN);
    GInputState.mouseButtonsPressed.insert(SDL_BUTTON_LEFT);

    // Tick and UI in one engine frame both see the same edges.
    GInputState.DiscardPressedBefore(41);
    CHECK(GInputState.keysPressed.contains(SDLK_RETURN));
    CHECK(GInputState.mouseButtonsPressed.contains(SDL_BUTTON_LEFT));

    // When UI was suppressed and did not call ClearPressed, the next frame expires them.
    GInputState.DiscardPressedBefore(42);
    CHECK(GInputState.keysPressed.empty());
    CHECK(GInputState.mouseButtonsPressed.empty());
    CHECK(GInputState.pressedFrame == FInputState::invalidPressedFrame);

    // A new event arriving before Tick clears stale edges but preserves the current one.
    GInputState.BeginPressedFrame(43);
    GInputState.keysPressed.insert(SDLK_SPACE);
    GInputState.BeginPressedFrame(44);
    GInputState.keysPressed.insert(SDLK_ESCAPE);
    CHECK_FALSE(GInputState.keysPressed.contains(SDLK_SPACE));
    CHECK(GInputState.keysPressed.contains(SDLK_ESCAPE));
}

TEST_CASE("physics bindings are safe without an engine", "[Unit][DotNet]")
{
    const FEngineApi api = BuildEngineApi();
    constexpr uint32_t invalidBodyId = 0xffffffffu;
    const FVec3 position{0.0f, 1.0f, 0.0f};

    CHECK(api.Physics_IsAvailable() == 0);
    CHECK(api.Physics_IsWorldPaused() == 0);
    CHECK(api.Physics_CreateSphereBody(&position, 0.5f, GkPhysicsMotionType::Dynamic) ==
          invalidBodyId);

    FPhysicsBodyState state{};
    api.Physics_GetBodyState(invalidBodyId, &state);
    CHECK(state.Valid == 0);
    CHECK(state.Rotation.W == 1.0f);

    // All mutations are explicitly no-ops for stale handles; none may reach the backend with an
    // invalid Jolt id or require an installed engine.
    api.Physics_SetBodyActive(invalidBodyId, 1);
    api.Physics_RemoveBody(invalidBodyId);
}
