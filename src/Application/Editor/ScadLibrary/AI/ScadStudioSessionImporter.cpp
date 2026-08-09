#include "Engine/Common/CoreMinimal.hpp"
#include "ScadStudioSessionImporter.hpp"

#include "Modules/ScadLoader/FScadSourceIndex.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace ScadLibrary::AI
{
    namespace
    {
        bool IsSafeRelativePath(const std::string& value)
        {
            const std::filesystem::path path(value);
            if (path.empty() || path.is_absolute())
            {
                return false;
            }
            return std::none_of(path.begin(), path.end(), [](const auto& component)
            { return component == ".."; });
        }
    }

    std::vector<FScadStudioImportCandidate> FScadStudioSessionImporter::Scan(
        const std::filesystem::path& workspace, std::vector<std::string>& outWarnings)
    {
        outWarnings.clear();
        std::vector<FScadStudioImportCandidate> result;
        try
        {
            std::ifstream indexInput(workspace / "sessions.json", std::ios::binary);
            if (!indexInput)
            {
                return result;
            }
            const nlohmann::json index = nlohmann::json::parse(indexInput);
            if (!index.is_object() || !index.contains("sessions") || !index["sessions"].is_array())
            {
                outWarnings.push_back("ScadStudio sessions.json 格式无效");
                return result;
            }
            for (const auto& entry : index["sessions"])
            {
                const std::string id = entry.value("id", "");
                if (id.empty() || entry.value("archived", false) ||
                    !std::all_of(id.begin(), id.end(), [](const char character)
                                 { return std::isalnum(static_cast<unsigned char>(character)) ||
                                     character == '-' || character == '_'; }))
                {
                    continue;
                }
                std::ifstream sessionInput(workspace / (id + ".json"), std::ios::binary);
                if (!sessionInput)
                {
                    outWarnings.push_back("缺少旧会话文件: " + id);
                    continue;
                }
                const nlohmann::json session = nlohmann::json::parse(sessionInput);
                FScadStudioImportCandidate candidate;
                candidate.id = id;
                candidate.title = session.value("title", entry.value("title", id));
                candidate.updatedAt = session.value("updatedAt", entry.value("updatedAt", int64_t{0}));
                candidate.activeFilePath = session.value("activeFilePath", "");
                candidate.source = session.value("currentSource", "");
                if (session.contains("files") && session["files"].is_array())
                {
                    candidate.fileCount = session["files"].size();
                    const nlohmann::json* selected = nullptr;
                    int selectedPriority = -1;
                    for (const auto& file : session["files"])
                    {
                        const std::string path = file.value("path", "");
                        if (!IsSafeRelativePath(path))
                        {
                            candidate.warnings.push_back("忽略不安全的项目路径: " + path);
                            continue;
                        }
                        const int priority = path == candidate.activeFilePath ? 2 : (path == "main.scad" ? 1 : 0);
                        if (!selected || priority > selectedPriority)
                        {
                            selected = &file;
                            selectedPriority = priority;
                        }
                    }
                    if (selected)
                    {
                        candidate.activeFilePath = selected->value("path", candidate.activeFilePath);
                        candidate.source = selected->value("source", candidate.source);
                    }
                }
                if (candidate.source.empty())
                {
                    candidate.warnings.push_back("会话没有可导入的 SCAD 源码");
                    continue;
                }
                Assets::Scad::FScadSourceIndex sourceIndex;
                std::string parseError;
                if (!Assets::Scad::BuildScadSourceIndex(candidate.source, sourceIndex, parseError))
                {
                    candidate.warnings.push_back("源码校验失败: " + parseError);
                    continue;
                }
                result.push_back(std::move(candidate));
            }
        }
        catch (const std::exception& exception)
        {
            outWarnings.push_back(exception.what());
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs)
        { return lhs.updatedAt > rhs.updatedAt; });
        return result;
    }
} // namespace ScadLibrary::AI
