#include "ScadSessionStore.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ScadStudio
{
    namespace
    {
        nlohmann::json TurnToJson(const FChatTurn& turn)
        {
            return nlohmann::json{
                {"role", turn.role},
                {"content", turn.content},
                {"scadSource", turn.scadSource},
                {"isError", turn.isError},
            };
        }

        FChatTurn TurnFromJson(const nlohmann::json& j)
        {
            FChatTurn turn;
            turn.role = j.value("role", "user");
            turn.content = j.value("content", "");
            turn.scadSource = j.value("scadSource", "");
            turn.isError = j.value("isError", false);
            return turn;
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
        return workspaceDir_ / (id + ".scad");
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
        out << doc.dump(2);
    }

    void ScadSessionStore::SaveIndex(const std::vector<FScadSession>& sessions) const
    {
        nlohmann::json index;
        nlohmann::json arr = nlohmann::json::array();
        for (const FScadSession& session : sessions)
        {
            arr.push_back(nlohmann::json{{"id", session.id}, {"title", session.title}});
        }
        index["sessions"] = std::move(arr);

        std::ofstream out(IndexPath(), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            SPDLOG_WARN("[ScadStudio] failed to write sessions.json");
            return;
        }
        out << index.dump(2);
    }

    void ScadSessionStore::DeleteSession(const std::string& id) const
    {
        std::error_code ec;
        std::filesystem::remove(JsonPath(id), ec);
        std::filesystem::remove(ScadPath(id), ec);
    }
}
