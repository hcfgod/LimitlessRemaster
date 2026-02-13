#include "Project/ProjectManager.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"
#include "Core/Debug/Log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Limitless::Project
{
    static std::string UtcNowIso8601()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const std::time_t t = system_clock::to_time_t(now);

        std::tm tmUtc{};
#if defined(LT_PLATFORM_WINDOWS)
        gmtime_s(&tmUtc, &t);
#else
        gmtime_r(&t, &tmUtc);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    ProjectManager& ProjectManager::GetInstance()
    {
        static ProjectManager s_Instance;
        return s_Instance;
    }

    Result<void> ProjectManager::OpenProjectRoot(const std::filesystem::path& projectRoot)
    {
        if (projectRoot.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "OpenProjectRoot: projectRoot is empty");
        }

        const std::filesystem::path root = std::filesystem::weakly_canonical(projectRoot);
        const std::filesystem::path projectFile = GetProjectFilePathForRoot(root);

        const auto loadResult = LoadProjectDefinitionFromFile(projectFile);
        if (loadResult.IsFailure())
        {
            return Result<void>(loadResult.GetError());
        }

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_ProjectRoot = root;
            m_Definition = loadResult.GetValue();
        }

        // Project switch must flush cross-project weak caches keyed by "Assets/..." keys.
        Assets::AssetManager::ClearCaches();

        // Make AssetPaths deterministic for the entire runtime/editor session.
        // Root directory should be the directory that contains `Assets/`.
        Assets::SetAssetRootDirectory(root);

        LT_CORE_INFO("Project opened: root='{}' name='{}' guid='{}'",
                     root.string(),
                     loadResult.GetValue().ProjectName,
                     loadResult.GetValue().ProjectGuid);

        return Result<void>();
    }

    Result<void> ProjectManager::CreateProjectRoot(const std::filesystem::path& projectRoot, const std::string& projectName)
    {
        if (projectRoot.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "CreateProjectRoot: projectRoot is empty");
        }
        if (projectName.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "CreateProjectRoot: projectName is empty");
        }

        const std::filesystem::path root = std::filesystem::weakly_canonical(projectRoot);

        try
        {
            std::filesystem::create_directories(root / "Assets");
            std::filesystem::create_directories(root / "Build");
            std::filesystem::create_directories(root / "Project");
            std::filesystem::create_directories(root / "Project" / "Settings");
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("CreateProjectRoot: failed to create directories: ") + e.what());
        }

        ProjectDefinition d;
        d.Version = 1;
        d.ProjectGuid = Assets::GenerateGuid();
        d.ProjectName = projectName;
        d.CreatedUtc = UtcNowIso8601();
        d.AssetRootRelative = "Assets";
        d.BuildRootRelative = "Build";
        d.SettingsVersion = 1;

        const std::filesystem::path projectFile = GetProjectFilePathForRoot(root);
        const auto save = SaveProjectDefinitionToFile(projectFile, d);
        if (save.IsFailure())
        {
            return save;
        }

        // Open immediately (sets AssetPaths override).
        return OpenProjectRoot(root);
    }

    void ProjectManager::CloseProject()
    {
        Assets::AssetManager::ClearCaches();
        Assets::SetAssetRootDirectory({});

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ProjectRoot.reset();
        m_Definition.reset();
    }

    bool ProjectManager::HasOpenProject() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_ProjectRoot.has_value();
    }

    std::filesystem::path ProjectManager::GetProjectRoot() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_ProjectRoot.value_or(std::filesystem::path{});
    }

    std::optional<ProjectDefinition> ProjectManager::GetProjectDefinition() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Definition;
    }
}

