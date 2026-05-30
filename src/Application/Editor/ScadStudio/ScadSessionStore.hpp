#pragma once

#include "ScadSession.hpp"

#include <filesystem>
#include <vector>

namespace ScadStudio
{
    // Loads/saves the SCAD Studio session list to a JSON index plus one JSON file per
    // session, under a workspace directory. Pure data; no engine dependency.
    class ScadSessionStore
    {
    public:
        explicit ScadSessionStore(std::filesystem::path workspaceDir);

        const std::filesystem::path& WorkspaceDir() const { return workspaceDir_; }

        // Load the index + every referenced session file. Missing/corrupt files are
        // skipped with a warning; never throws.
        std::vector<FScadSession> LoadAll() const;

        // Persist one session's JSON (messages + current source). Best-effort.
        void SaveSession(const FScadSession& session) const;

        // Rewrite the index (id + title + order). Best-effort.
        void SaveIndex(const std::vector<FScadSession>& sessions) const;

        // Delete a session's JSON and .scad files. Best-effort.
        void DeleteSession(const std::string& id) const;

        std::filesystem::path ScadPath(const std::string& id) const;

    private:
        std::filesystem::path JsonPath(const std::string& id) const;
        std::filesystem::path IndexPath() const;

        std::filesystem::path workspaceDir_;
    };
}
