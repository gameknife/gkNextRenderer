#include "Engine/Runtime/Reflection/ReflectionRegistry.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"
#include "Engine/Runtime/Reflection/PropertyTypes.hpp"
#include "Modules/NextDotNet/EngineApi.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

// src/Modules/NextDotNet/ReflectionManifest.json is a committed snapshot of what the engine
// registers with entt::meta, and assets/csharp/GkNext.Engine/Components.g.cs is generated from it
// with every property id baked in as a compile-time constant.
//
// That is what makes a property write cheap, and it is also what makes staleness dangerous: a
// renamed or removed property would leave the generated C# addressing an id that no longer
// resolves, which surfaces as a logged warning and a value that silently never changes. This test
// is the guard — it fails the moment reflection and the snapshot disagree, and the fix is
// `gnb csharpgen --refresh`.

using namespace Modules::NextDotNet;

namespace
{
    std::filesystem::path ManifestPath()
    {
        return std::filesystem::path(GK_NEXT_SOURCE_DIR) / "src" / "Modules" / "NextDotNet" /
               "ReflectionManifest.json";
    }

    /// The manifest as {type name -> {property name -> property}}, which is how the comparison
    /// below wants to read it. Ordering is irrelevant here; the dump sorts only so the committed
    /// file diffs cleanly.
    using PropertyMap = std::map<std::string, nlohmann::json>;
    using TypeMap = std::map<std::string, PropertyMap>;
}

TEST_CASE("the committed reflection manifest matches live reflection", "[Unit][DotNet][Reflection]")
{
    const std::filesystem::path path = ManifestPath();
    REQUIRE(std::filesystem::exists(path));

    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream);
    nlohmann::json manifest;
    REQUIRE_NOTHROW(stream >> manifest);
    CHECK(manifest.value("version", 0) == 1);

    TypeMap recorded;
    std::map<std::string, uint32_t> recordedTypeIds;
    for (const auto& entry : manifest["types"])
    {
        const std::string name = entry.at("name").get<std::string>();
        recordedTypeIds[name] = entry.at("typeId").get<uint32_t>();
        for (const auto& property : entry.at("properties"))
        {
            recorded[name][property.at("name").get<std::string>()] = property;
        }
    }

    Reflection::RegisterAllReflection();
    const auto& live = Reflection::GetReflectedTypes();
    REQUIRE_FALSE(live.empty());

    // A type registered but absent from the manifest means the snapshot predates it, and the
    // generated wrappers are missing a whole component.
    CHECK(recorded.size() == live.size());

    for (const Reflection::FReflectedType& reflected : live)
    {
        INFO("type " << reflected.name);
        REQUIRE(recorded.count(reflected.name) == 1);
        CHECK(recordedTypeIds[reflected.name] == reflected.meta.id());

        const PropertyMap& properties = recorded[reflected.name];
        const std::vector<Reflection::PropertyInfo> liveProperties =
            Reflection::PropertyAccessor::GetProperties(reflected.meta);
        CHECK(properties.size() == liveProperties.size());

        for (const Reflection::PropertyInfo& info : liveProperties)
        {
            INFO("property " << reflected.name << "." << info.name);
            const auto found = properties.find(info.name);
            REQUIRE(found != properties.end());

            // propId and type are what the generated C# hard-codes. displayName, tooltip and the
            // rest are documentation and are deliberately not compared: they change often and a
            // reworded tooltip should not fail the build.
            CHECK(found->second.at("propId").get<uint32_t>() == info.propId);
            CHECK(found->second.at("type").get<std::string>() ==
                  std::string(Reflection::PropertyTypeToString(info.type)));
            CHECK(found->second.at("readOnly").get<bool>() == info.meta.IsReadOnly());
            CHECK(found->second.at("scriptExposed").get<bool>() == info.meta.IsScriptExposed());
        }
    }
}

TEST_CASE("component property bindings are safe without a scene", "[Unit][DotNet][Reflection]")
{
    const FEngineApi api = BuildEngineApi();

    // Every accessor is reachable from managed code the moment a game holds a stale node id, so
    // none of them may dereference their way into a crash when the node is gone.
    constexpr uint32_t missingNode = 0xFFFFFFFEu;
    constexpr uint32_t renderComponent = 3393791048u;  // "RenderComponent"_hs
    constexpr uint32_t someProperty = 1495943489u;     // RenderComponent::Visible

    CHECK(api.Component_Has(missingNode, renderComponent) == 0);
    CHECK(api.Component_GetBool(missingNode, renderComponent, someProperty) == 0);
    CHECK(api.Component_GetInt32(missingNode, renderComponent, someProperty) == 0);
    CHECK(api.Component_GetUInt32(missingNode, renderComponent, someProperty) == 0u);
    CHECK(api.Component_GetFloat(missingNode, renderComponent, someProperty) == 0.0f);
    CHECK(api.Component_GetDouble(missingNode, renderComponent, someProperty) == 0.0);
    CHECK(api.Component_GetString(missingNode, renderComponent, someProperty, nullptr, 0) == 0);

    FVec4 vector{1.0f, 1.0f, 1.0f, 1.0f};
    api.Component_GetVec4(missingNode, renderComponent, someProperty, &vector);
    CHECK(vector.X == 0.0f);

    // Setters have no return value, so "safe" here means "does not crash and does not write".
    const FVec3 translation{1.0f, 2.0f, 3.0f};
    api.Component_SetVec3(missingNode, renderComponent, someProperty, &translation);
    api.Component_SetBool(missingNode, renderComponent, someProperty, 1);
    api.Component_SetString(missingNode, renderComponent, someProperty, GkStr{nullptr, 0});

    // Null struct pointers are the other way a managed bug arrives here.
    api.Component_SetVec3(missingNode, renderComponent, someProperty, nullptr);
    api.Component_GetVec3(missingNode, renderComponent, someProperty, nullptr);
}

TEST_CASE("the manifest type ids the generator hard-codes are the registered hashes",
          "[Unit][DotNet][Reflection]")
{
    Reflection::RegisterAllReflection();

    // The generated C# addresses a component by entt::hashed_string(name). Register() already
    // checks this at startup, but a failure there is only a log line in a running engine; here it
    // is a failing test.
    for (const Reflection::FReflectedType& reflected : Reflection::GetReflectedTypes())
    {
        INFO("type " << reflected.name);
        CHECK(reflected.meta.id() == entt::hashed_string::value(reflected.name.c_str()));
    }
}
