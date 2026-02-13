#pragma once

#include "Core/Error.h"
#include "Project/ProjectDefinition.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace Limitless::Project
{
    /**
     * @brief Editor/tooling-facing project manager.
     *
     * Owns the "currently open" project context:
     * - project root directory
     * - loaded `ProjectDefinition`
     *
     * When a project is opened, this manager also sets the AssetPaths root override so the
     * rest of the engine can resolve `Assets/...` deterministically.
     */
    class ProjectManager final
    {
    public:
        static ProjectManager& GetInstance();

        /// Opens an existing project located at `projectRoot` (expects `Project/Project.json`).
        Result<void> OpenProjectRoot(const std::filesystem::path& projectRoot);

        /// Creates a new project at `projectRoot`, writing `Project/Project.json` and ensuring core folders exist.
        Result<void> CreateProjectRoot(const std::filesystem::path& projectRoot, const std::string& projectName);

        /// Clears the current project (does not delete anything on disk).
        void CloseProject();

        /// Updates the project's default scene reference and persists Project/Project.json.
        Result<void> SetDefaultSceneAssetKey(const std::string& sceneAssetKey);

        [[nodiscard]] bool HasOpenProject() const;
        [[nodiscard]] std::filesystem::path GetProjectRoot() const;
        [[nodiscard]] std::optional<ProjectDefinition> GetProjectDefinition() const;

    private:
        ProjectManager() = default;

    private:
        mutable std::mutex m_Mutex;
        std::optional<std::filesystem::path> m_ProjectRoot;
        std::optional<ProjectDefinition> m_Definition;
    };
}

