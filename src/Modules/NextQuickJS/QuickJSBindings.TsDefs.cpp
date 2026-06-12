// QuickJS TypeScript toolchain helpers: source timestamp scanning and
// Engine.d.ts definition generation. Development-time only.
// Split from QuickJSBindings.cpp; same namespace, separate TU.
#include "Modules/NextQuickJS/QuickJSEngine.hpp"
#include "Modules/NextQuickJS/QuickJSBindings.Internal.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Runtime/Reflection/PropertyAccessor.h"
#include "Modules/NextQuickJS/Reflection/QuickJSReflectionBridge.hpp"
#include "Modules/NextQuickJS/Reflection/QuickJSTypeConverter.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.h"
#include "Engine/Runtime/Utilities/JsonHelpers.h"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/stopwatch.h>
#include <fstream>
#include <entt/core/hashed_string.hpp>
#include <cstdlib>
#include <limits>

#if WITH_QUICKJS
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#endif

#if WITH_QUICKJS
namespace NextQuickJSBindings
{
    bool HasExtension(const std::filesystem::path& path, std::initializer_list<const char*> extensions)
    {
        const std::string extension = path.extension().string();
        for (const char* candidate : extensions)
        {
            if (extension == candidate)
            {
                return true;
            }
        }
        return false;
    }

    bool FindLatestTimestamp(const std::filesystem::path& root,
        std::initializer_list<const char*> extensions,
        std::filesystem::file_time_type& outTimestamp)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(root, ec))
        {
            return false;
        }

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            SPDLOG_WARN("Failed to enumerate {}: {}", root.string(), ec.message());
            return false;
        }

        const fs::recursive_directory_iterator end;
        bool hasTimestamp = false;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                SPDLOG_WARN("Directory iteration error under {}: {}", root.string(), ec.message());
                ec.clear();
                continue;
            }

            if (it->is_directory(ec))
            {
                if (!ec && it->path().filename() == "node_modules")
                {
                    it.disable_recursion_pending();
                }
                ec.clear();
                continue;
            }

            if (ec)
            {
                SPDLOG_WARN("Failed to inspect {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }

            if (ec)
            {
                SPDLOG_WARN("Failed to query file type for {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!HasExtension(it->path(), extensions))
            {
                continue;
            }

            auto timestamp = it->last_write_time(ec);
            if (ec)
            {
                SPDLOG_WARN("Failed to query timestamp for {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!hasTimestamp || timestamp > outTimestamp)
            {
                outTimestamp = timestamp;
                hasTimestamp = true;
            }
        }

        return hasTimestamp;
    }

    bool GetLatestTypeScriptTimestamp(const std::filesystem::path& projectDir,
        const std::filesystem::path& tsconfigPath,
        std::filesystem::file_time_type& outTimestamp)
    {
        outTimestamp = std::filesystem::file_time_type{};
        if (!FindLatestTimestamp(projectDir, { ".ts", ".d.ts" }, outTimestamp))
        {
            return false;
        }

        std::error_code ec;
        if (!tsconfigPath.empty() && std::filesystem::exists(tsconfigPath, ec))
        {
            auto tsconfigTime = std::filesystem::last_write_time(tsconfigPath, ec);
            if (!ec && tsconfigTime > outTimestamp)
            {
                outTimestamp = tsconfigTime;
            }
        }

        return true;
    }

    TypeScriptPaths ResolveTypeScriptPaths()
    {
        namespace fs = std::filesystem;

        TypeScriptPaths paths;
        paths.outputDir = fs::absolute(fs::path(Utilities::FileHelper::GetPlatformFilePath("assets/scripts")));

        fs::path tsconfigPath = fs::path(Utilities::FileHelper::GetNormalizedFilePath("assets/typescript/tsconfig.json"));
#if defined(GK_NEXT_SOURCE_DIR)
        const fs::path sourceRoot = fs::path(GK_NEXT_SOURCE_DIR);
        const fs::path sourceTsconfig = sourceRoot / "assets/typescript/tsconfig.json";
        if (fs::exists(sourceTsconfig))
        {
            tsconfigPath = sourceTsconfig;
        }
#endif

        paths.tsconfigPath = tsconfigPath;
        paths.projectDir = tsconfigPath.parent_path();
        return paths;
    }

    bool HasNewerTypeScriptSources(const std::filesystem::path& projectDir,
        const std::filesystem::path& outputDir,
        const std::filesystem::path& tsconfigPath,
        std::filesystem::file_time_type& outLatestSource)
    {
        if (!GetLatestTypeScriptTimestamp(projectDir, tsconfigPath, outLatestSource))
        {
            return false;
        }

        const std::filesystem::path stampPath = outputDir / ".tsc.stamp";
        std::error_code ec;
        if (std::filesystem::exists(stampPath, ec))
        {
            auto stampTime = std::filesystem::last_write_time(stampPath, ec);
            if (!ec && stampTime >= outLatestSource)
            {
                return false;
            }
        }

        return true;
    }

    std::string NormalizeLineEndings(std::string text)
    {
        std::string normalized;
        normalized.reserve(text.size());
        for (size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] == '\r')
            {
                if (index + 1 < text.size() && text[index + 1] == '\n')
                {
                    ++index;
                }
                normalized += '\n';
                continue;
            }

            normalized += text[index];
        }

        return normalized;
    }

    std::string DetectLineEnding(const std::string& text)
    {
        if (text.find("\r\n") != std::string::npos)
        {
            return "\r\n";
        }

        if (text.find('\n') != std::string::npos)
        {
            return "\n";
        }

        return "\n";
    }

    std::string ApplyLineEnding(const std::string& text, std::string_view lineEnding)
    {
        if (lineEnding == "\n")
        {
            return text;
        }

        std::string converted;
        converted.reserve(text.size() + text.size() / 8);
        for (char ch : text)
        {
            if (ch == '\n')
            {
                converted.append(lineEnding);
            }
            else
            {
                converted += ch;
            }
        }

        return converted;
    }

    std::filesystem::path FindProjectRoot()
    {
        std::filesystem::path cursor = std::filesystem::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            if (std::filesystem::exists(cursor / "AGENTS.md") && std::filesystem::exists(cursor / "assets"))
            {
                return cursor;
            }
            if (!cursor.has_parent_path())
            {
                break;
            }
            cursor = cursor.parent_path();
        }
        return std::filesystem::current_path();
    }

    std::string BuildTypeScriptDefinitions()
    {
        std::string result;
        result += "export interface Vec2 { x: number; y: number; }\n";
        result += "export interface Vec3 { x: number; y: number; z: number; }\n";
        result += "export interface Vec4 { x: number; y: number; z: number; w: number; }\n";
        result += "export interface Quat { x: number; y: number; z: number; w: number; }\n\n";

        result += "export class NextEngine {\n";
        result += "    GetTotalFrames(): number;\n";
        result += "    GetTime(): number;\n";
        result += "    GetDeltaSeconds(): number;\n";
        result += "    GetSmoothDeltaSeconds(): number;\n";
        result += "}\n";
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Assets::Node>("Node");
        result += "export interface Node {\n";
        result += "    RecalcTransform(full?: boolean): void;\n";
        result += "}\n";
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Assets::Scene>("Scene");
        result += "export interface Scene {\n";
        result += "    GetNodeById(nodeId: number): Node;\n";
        result += "    AddLambertianMaterial(color: Vec3): number;\n";
        result += "    AddDiffuseLightMaterial(color: Vec3, intensity?: number): number;\n";
        result += "    AddRenderNode(spec: RenderNodeSpec): number;\n";
        result += "    RemoveNodeById(nodeId: number): void;\n";
        result += "    MarkTransformDirty(): void;\n";
        result += "}\n";

        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::RenderComponent>("RenderComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::PhysicsComponent>("PhysicsComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::SkinnedMeshComponent>("SkinnedMeshComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateEnumTypeScriptDef<Runtime::ENodeMobility>("ENodeMobility");

        result += "\nexport namespace Global {\n";
        result += "    function spdlog(level: string, ...args: any[]): void;\n";
        result += "    function GetEngine(): NextEngine;\n";
        result += "    function GetScene(): Scene;\n";
        result += "}\n";
        result += "\nexport namespace Input {\n";
        result += "    function IsKeyDown(name: string): boolean;\n";
        result += "    function IsKeyPressed(name: string): boolean;\n";
        result += "    function IsMouseButtonDown(button: number): boolean;\n";
        result += "    function IsMouseButtonPressed(button: number): boolean;\n";
        result += "    function GetGamepadButton(name: string): boolean;\n";
        result += "}\n";
        result += "\nexport namespace Audio {\n";
        result += "    function PlaySfx(path: string, volume?: number): void;\n";
        result += "    function PlayMusic(path: string, volume?: number): void;\n";
        result += "    function StopMusic(): void;\n";
        result += "}\n";
        result += "\nexport namespace UI {\n";
        result += "    function Begin(name: string, flags?: number): boolean;\n";
        result += "    function End(): void;\n";
        result += "    function Text(text: string): void;\n";
        result += "    function SetCursorPos(x: number, y: number): void;\n";
        result += "    function GetWindowSize(): Vec2;\n";
        result += "    function SetWindowFontScale(scale: number): void;\n";
        result += "    function GetScreenSize(): Vec2;\n";
        result += "    function CalcTextSize(text: string, scale?: number): Vec2;\n";
        result += "    function DrawText(text: string, x: number, y: number, scale?: number, r?: number, g?: number, b?: number, a?: number): void;\n";
        result += "    function DrawRectFilled(x: number, y: number, width: number, height: number, r: number, g: number, b: number, a: number, rounding?: number): void;\n";
        result += "    function DrawRect(x: number, y: number, width: number, height: number, r: number, g: number, b: number, a: number, rounding?: number, thickness?: number): void;\n";
        result += "}\n";
        result += "\nexport type ProceduralModelSpec =\n";
        result += "    | { type: \"box\"; min: Vec3; max: Vec3 }\n";
        result += "    | { type: \"sphere\"; center?: Vec3; radius: number };\n";
        result += "export interface RenderNodeSpec {\n";
        result += "    name: string;\n";
        result += "    modelId: number;\n";
        result += "    materialId: number;\n";
        result += "    translation?: Vec3;\n";
        result += "    scale?: Vec3;\n";
        result += "    visible?: boolean;\n";
        result += "}\n";
        result += "\nexport namespace SceneBuild {\n";
        result += "    function AddProceduralModel(spec: ProceduralModelSpec): number;\n";
        result += "    function AddLambertianMaterial(color: Vec3): number;\n";
        result += "    function AddDiffuseLightMaterial(color: Vec3, intensity?: number): number;\n";
        result += "    function AddRenderNode(spec: RenderNodeSpec): number;\n";
        result += "}\n";
        result += "export type InputEventType = \"keyDown\" | \"keyUp\" | \"mouseButtonDown\" | \"mouseButtonUp\" | \"gamepadButtonDown\" | \"gamepadButtonUp\";\n";
        result += "export interface InputEvent {\n";
        result += "    type: InputEventType;\n";
        result += "    key?: string;\n";
        result += "    mouseButton?: number;\n";
        result += "    gamepadButton?: string;\n";
        result += "    repeated?: boolean;\n";
        result += "}\n";
        result += "\nexport interface LifecycleHooks {\n";
        result += "    onInit?: () => void;\n";
        result += "    onDestroy?: () => void;\n";
        result += "    onBeforeSceneRebuild?: () => void;\n";
        result += "    onSceneLoaded?: () => void;\n";
        result += "    onRenderUI?: () => boolean | void;\n";
        result += "    onInputEvent?: (event: InputEvent) => boolean | void;\n";
        result += "}\n";
        result += "export interface CameraOverride { position: Vec3; target: Vec3; up: Vec3; fov: number; }\n";
        result += "export function RegisterLifecycleHooks(hooks: LifecycleHooks): void;\n";
        result += "export function RegisterTickCallback(callback: (deltaSeconds: number) => void): void;\n";
        result += "export function LoadJson(path: string): any;\n";
        result += "export function RequestLoadScene(filename: string): void;\n";
        result += "export function RequestClose(): void;\n";
        result += "export function GetScreenSize(): Vec2;\n";
        result += "export function SetOverrideCamera(camera: CameraOverride): void;\n";
        result += "export function IsReplayMode(): boolean;\n";
        result += "export function WriteFile(path: string, content: string): void;\n";

        return result;
    }

    void UpdateTypeScriptDefinitions(const std::filesystem::path& tsconfigPath)
    {
        namespace fs = std::filesystem;

        if (tsconfigPath.empty())
        {
            return;
        }

        const fs::path outputPath = tsconfigPath.parent_path() / "Engine.d.ts";
        const std::string content = BuildTypeScriptDefinitions();

        std::string existing;
        std::string lineEnding = "\n";
        std::ifstream reader(outputPath, std::ios::binary);
        if (reader)
        {
            existing.assign((std::istreambuf_iterator<char>(reader)), std::istreambuf_iterator<char>());
            lineEnding = DetectLineEnding(existing);

            if (NormalizeLineEndings(existing) == NormalizeLineEndings(content))
            {
                return;
            }
        }

        std::ofstream writer(outputPath, std::ios::binary);
        if (!writer)
        {
            SPDLOG_WARN("Failed to write TypeScript definitions to {}", outputPath.string());
            return;
        }

        writer << ApplyLineEnding(content, lineEnding);
    }
}
#endif
