#include "ScadSessionStore.hpp"
#include "ScadStudioUtils.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

namespace ScadStudio
{
    namespace
    {
        constexpr auto kJsonWriteErrorHandler = nlohmann::json::error_handler_t::replace;

        nlohmann::json TurnToJson(const FChatTurn& turn)
        {
            nlohmann::json files = nlohmann::json::array();
            for (const FScadProjectFile& file : turn.files)
            {
                files.push_back(nlohmann::json{{"path", file.path}, {"source", file.source}});
            }
            return nlohmann::json{
                {"role", turn.role},
                {"content", turn.content},
                {"scadSource", turn.scadSource},
                {"files", std::move(files)},
                {"targetFilePath", turn.targetFilePath},
                {"isError", turn.isError},
            };
        }

        std::vector<FScadProjectFile> FilesFromJson(const nlohmann::json& j)
        {
            std::vector<FScadProjectFile> files;
            if (!j.is_array())
            {
                return files;
            }
            for (const auto& f : j)
            {
                FScadProjectFile file;
                file.path = f.value("path", "");
                file.source = f.value("source", "");
                if (!file.path.empty())
                {
                    files.push_back(std::move(file));
                }
            }
            return files;
        }

        FChatTurn TurnFromJson(const nlohmann::json& j)
        {
            FChatTurn turn;
            turn.role = j.value("role", "user");
            turn.content = j.value("content", "");
            turn.scadSource = j.value("scadSource", "");
            if (j.contains("files"))
            {
                turn.files = FilesFromJson(j["files"]);
            }
            turn.targetFilePath = j.value("targetFilePath", "");
            turn.isError = j.value("isError", false);
            return turn;
        }

        int64_t FileTimeUnixSeconds(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto ft = std::filesystem::last_write_time(path, ec);
            if (ec)
            {
                return NowUnixSeconds();
            }
            const auto st = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            return std::chrono::duration_cast<std::chrono::seconds>(st.time_since_epoch()).count();
        }
    } // namespace

    ScadSessionStore::ScadSessionStore(std::filesystem::path workspaceDir)
        : workspaceDir_(std::move(workspaceDir))
    {
        std::error_code ec;
        std::filesystem::create_directories(workspaceDir_, ec);
    }

    std::filesystem::path ScadSessionStore::IndexPath() const
    {
        return workspaceDir_ / "sessions.json";
    }

    std::filesystem::path ScadSessionStore::JsonPath(const std::string& id) const
    {
        return workspaceDir_ / (id + ".json");
    }

    std::filesystem::path ScadSessionStore::ScadPath(const std::string& id) const
    {
        return ProjectDir(id) / "main.scad";
    }

    std::filesystem::path ScadSessionStore::LegacyScadPath(const std::string& id) const
    {
        return workspaceDir_ / (id + ".scad");
    }

    std::filesystem::path ScadSessionStore::ProjectDir(const std::string& id) const
    {
        return workspaceDir_ / id;
    }

    std::vector<FScadSession> ScadSessionStore::LoadAll() const
    {
        std::vector<FScadSession> sessions;

        std::ifstream indexFile(IndexPath(), std::ios::binary);
        if (!indexFile)
        {
            return sessions; // first run: no index yet
        }

        nlohmann::json index;
        try
        {
            indexFile >> index;
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("[ScadStudio] sessions.json parse failed: {}", e.what());
            return sessions;
        }

        if (!index.is_object() || !index.contains("sessions") || !index["sessions"].is_array())
        {
            return sessions;
        }

        for (const auto& entry : index["sessions"])
        {
            const std::string id = entry.value("id", "");
            if (id.empty())
            {
                continue;
            }
            if (entry.value("archived", false))
            {
                continue;
            }

            std::ifstream sessionFile(JsonPath(id), std::ios::binary);
            if (!sessionFile)
            {
                SPDLOG_WARN("[ScadStudio] session file missing for id {}", id);
                continue;
            }

            nlohmann::json doc;
            try
            {
                sessionFile >> doc;
            }
            catch (const std::exception& e)
            {
                SPDLOG_WARN("[ScadStudio] session {} parse failed: {}", id, e.what());
                continue;
            }

            FScadSession session;
            session.id = id;
            session.title = doc.value("title", entry.value("title", id));
            session.currentSource = doc.value("currentSource", "");
            session.createdAt = doc.value("createdAt", entry.value("createdAt", int64_t{0}));
            session.updatedAt = doc.value("updatedAt", entry.value("updatedAt", int64_t{0}));
            session.archived = doc.value("archived", entry.value("archived", false));
            if (session.archived)
            {
                continue;
            }
            const int64_t fallbackTime = FileTimeUnixSeconds(JsonPath(id));
            if (session.createdAt <= 0)
            {
                session.createdAt = fallbackTime;
            }
            if (session.updatedAt <= 0)
            {
                session.updatedAt = fallbackTime;
            }
            if (doc.contains("files"))
            {
                session.files = FilesFromJson(doc["files"]);
            }
            session.activeFilePath = doc.value("activeFilePath", "");
            if (doc.contains("turns") && doc["turns"].is_array())
            {
                for (const auto& t : doc["turns"])
                {
                    session.turns.push_back(TurnFromJson(t));
                }
            }
            sessions.push_back(std::move(session));
        }

        return sessions;
    }

    void ScadSessionStore::SaveSession(const FScadSession& session) const
    {
        nlohmann::json doc;
        doc["title"] = session.title;
        doc["currentSource"] = session.currentSource;
        doc["activeFilePath"] = session.activeFilePath;
        doc["createdAt"] = session.createdAt;
        doc["updatedAt"] = session.updatedAt;
        doc["archived"] = session.archived;
        nlohmann::json files = nlohmann::json::array();
        for (const FScadProjectFile& file : session.files)
        {
            files.push_back(nlohmann::json{{"path", file.path}, {"source", file.source}});
        }
        doc["files"] = std::move(files);
        nlohmann::json turns = nlohmann::json::array();
        for (const FChatTurn& turn : session.turns)
        {
            turns.push_back(TurnToJson(turn));
        }
        doc["turns"] = std::move(turns);

        std::ofstream out(JsonPath(session.id), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            SPDLOG_WARN("[ScadStudio] failed to write session {}", session.id);
            return;
        }
        try
        {
            out << doc.dump(2, ' ', false, kJsonWriteErrorHandler);
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("[ScadStudio] failed to serialize session {}: {}", session.id, e.what());
        }
    }

    void ScadSessionStore::SaveIndex(const std::vector<FScadSession>& sessions) const
    {
        nlohmann::json index;
        nlohmann::json arr = nlohmann::json::array();
        for (const FScadSession& session : sessions)
        {
            if (session.archived)
            {
                continue;
            }
            arr.push_back(nlohmann::json{
                {"id", session.id},
                {"title", session.title},
                {"createdAt", session.createdAt},
                {"updatedAt", session.updatedAt},
                {"archived", false},
            });
        }
        index["sessions"] = std::move(arr);

        std::ofstream out(IndexPath(), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            SPDLOG_WARN("[ScadStudio] failed to write sessions.json");
            return;
        }
        try
        {
            out << index.dump(2, ' ', false, kJsonWriteErrorHandler);
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("[ScadStudio] failed to serialize sessions.json: {}", e.what());
        }
    }

    void ScadSessionStore::DeleteSession(const std::string& id) const
    {
        std::error_code ec;
        std::filesystem::remove(JsonPath(id), ec);
        std::filesystem::remove(ScadPath(id), ec);
        std::filesystem::remove(LegacyScadPath(id), ec);
        std::filesystem::remove_all(ProjectDir(id), ec);
    }
}
