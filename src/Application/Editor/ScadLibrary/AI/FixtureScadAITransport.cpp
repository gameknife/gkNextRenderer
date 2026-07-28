#include "Engine/Common/CoreMinimal.hpp"
#include "FixtureScadAITransport.hpp"

#include <nlohmann/json.hpp>

namespace ScadLibrary::AI
{
    namespace
    {
        void FillFixtureConfiguration(FScadAITransportConfiguration& configuration)
        {
            configuration = {};
            configuration.providers.push_back(
                {"fixture", "Agent Validation Fixture", {"deterministic"}, true, true});
            configuration.currentProviderId = "fixture";
            configuration.currentModelId = "deterministic";
            configuration.statusMessage = "确定性离线验证模式";
        }
    }

    bool FFixtureScadAITransport::LoadConfiguration(FScadAITransportConfiguration& outConfiguration,
                                                     std::string& outError)
    {
        FillFixtureConfiguration(outConfiguration);
        outError.clear();
        return true;
    }

    bool FFixtureScadAITransport::SelectProvider(const std::string& providerId,
                                                  FScadAITransportConfiguration& outConfiguration,
                                                  std::string& outError)
    {
        FillFixtureConfiguration(outConfiguration);
        if (providerId == "fixture")
        {
            outError.clear();
            return true;
        }
        outError = "Agent validation 仅支持 fixture Provider";
        return false;
    }

    bool FFixtureScadAITransport::SelectModel(const std::string& modelId,
                                               FScadAITransportConfiguration& outConfiguration,
                                               std::string& outError)
    {
        FillFixtureConfiguration(outConfiguration);
        if (modelId == "deterministic")
        {
            outError.clear();
            return true;
        }
        outError = "Agent validation 仅支持 deterministic 模型";
        return false;
    }

    NextAI::FChatResponse FFixtureScadAITransport::Complete(const NextAI::FChatRequest& request,
                                                            NextAI::FChatStreamCallback onDelta)
    {
        if (request.messages.empty())
        {
            return NextAI::FChatResponse::Failure("fixture request has no messages");
        }
        nlohmann::json envelope;
        try
        {
            envelope = nlohmann::json::parse(request.messages.back().content);
        }
        catch (const std::exception& exception)
        {
            return NextAI::FChatResponse::Failure(exception.what());
        }
        const nlohmann::json snapshot = envelope.value("snapshot", nlohmann::json::object());
        nlohmann::json artifact{{"version", 1}, {"summary", "Agent validation fixture proposal"}};
        if (request.responseSchemaName == "scad_scene_source_v1")
        {
            artifact["source"] = snapshot.value("source", "cube(1);");
        }
        else if (request.responseSchemaName == "scad_kit_module_v1")
        {
            artifact["moduleName"] = snapshot.value("moduleName", "");
            artifact["moduleSource"] = snapshot.value("moduleSource", "");
        }
        else if (request.responseSchemaName == "scad_scene_objects_v1")
        {
            artifact["operations"] = nlohmann::json::array();
            const auto objects = snapshot.value("objects", nlohmann::json::array());
            if (!objects.empty())
            {
                artifact["operations"].push_back(
                    {{"type", "update"}, {"id", objects.front().value("id", "o0")},
                     {"changes", {{"position", objects.front().value("position", nlohmann::json::array({0, 0, 0}))}}}});
            }
            else
            {
                const auto catalog = snapshot.value("catalog", nlohmann::json::array());
                if (catalog.empty())
                {
                    return NextAI::FChatResponse::Failure("fixture scene has no objects or catalog");
                }
                artifact["operations"].push_back(
                    {{"type", "add"}, {"newId", "fixture0"},
                     {"object", {{"kitIndex", catalog.front().value("kitIndex", 0)},
                                 {"module", catalog.front().value("module", "")},
                                 {"position", {0, 0, 0}}, {"rotation", {0, 0, 0}},
                                 {"scale", {1, 1, 1}}, {"arguments", ""}}}});
            }
        }
        else if (request.responseSchemaName == "scad_terrain_operations_v1")
        {
            artifact["operations"] = nlohmann::json::array(
                {{{"type", "set_terrain"},
                  {"changes", {{"roughness", snapshot["terrain"].value("roughness", 0.5)}}}}});
        }
        else if (request.responseSchemaName == "scad_rig_clip_operations_v1")
        {
            const auto clips = snapshot.value("clips", nlohmann::json::array());
            if (clips.empty())
            {
                return NextAI::FChatResponse::Failure("fixture rig has no clips");
            }
            artifact["operations"] = nlohmann::json::array(
                {{{"type", "set_clip_meta"}, {"id", clips.front().value("id", "")},
                  {"changes", {{"loop", clips.front().value("loop", true)}}}}});
        }
        else
        {
            return NextAI::FChatResponse::Failure("unknown fixture schema");
        }
        const std::string content = artifact.dump();
        if (onDelta)
        {
            onDelta(content);
        }
        return NextAI::FChatResponse::Success(content);
    }

    bool FFixtureScadAITransport::Cancel(const std::string&)
    {
        return true;
    }
} // namespace ScadLibrary::AI
