#pragma once

#include "Core/Error.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Limitless::Editor
{
    struct RecentProjectEntry final
    {
        std::string ProjectRoot;
        std::string ProjectName;
        std::string LastOpenedUtc;
    };

    /// Persistent "recent projects" list stored in the user data directory.
    ///
    /// This is editor-only UX state and should never be stored under a project directory.
    class EditorRecentProjects final
    {
    public:
        static EditorRecentProjects& GetInstance();

        /// Loads from disk (best-effort). Safe to call multiple times.
        void EnsureLoaded();

        /// Adds or updates an entry and persists to disk (best-effort).
        void AddOrUpdate(const std::filesystem::path& projectRoot, const std::string& projectName);

        void Remove(const std::filesystem::path& projectRoot);

        [[nodiscard]] std::vector<RecentProjectEntry> GetEntries() const;

        /// Returns the persistent file location, or empty path if user data path is unavailable.
        [[nodiscard]] std::filesystem::path GetStoragePath() const;

    private:
        EditorRecentProjects() = default;

        static std::string UtcNowIso8601();
        void SaveToDiskBestEffort() const;

    private:
        mutable std::mutex m_Mutex;
        bool m_Loaded = false;
        std::vector<RecentProjectEntry> m_Entries;
    };
}

