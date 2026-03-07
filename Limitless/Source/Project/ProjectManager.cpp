#include "Project/ProjectManager.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"
#include "Core/Debug/Log.h"
#include "Scripting/NativeScriptRegistry.h"

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
        NativeScriptRegistry::Clear();

        // Make AssetPaths deterministic for the entire runtime/editor session.
        // Root directory should be the directory that contains `Assets/`.
        Assets::SetAssetRootDirectory(root);
        Assets::AssetDatabase::GetInstance().Reset();

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
        NativeScriptRegistry::Clear();
        Assets::AssetDatabase::GetInstance().Reset();
        Assets::SetAssetRootDirectory({});

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ProjectRoot.reset();
        m_Definition.reset();
    }

    Result<void> ProjectManager::SetDefaultSceneAssetKey(const std::string& sceneAssetKey)
    {
        if (sceneAssetKey.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "SetDefaultSceneAssetKey: sceneAssetKey is empty");
        }

        std::filesystem::path projectRoot;
        ProjectDefinition definition;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (!m_ProjectRoot.has_value() || !m_Definition.has_value())
            {
                return Result<void>(ErrorCode::InvalidState, "SetDefaultSceneAssetKey: no project is currently open");
            }

            projectRoot = *m_ProjectRoot;
            definition = *m_Definition;
        }

        definition.DefaultScene.Key = sceneAssetKey;
        definition.DefaultScene.Guid.clear();

        const auto recordResult = Assets::AssetDatabase::GetInstance().FindByKey(sceneAssetKey);
        if (recordResult.IsSuccess())
        {
            definition.DefaultScene.Guid = recordResult.GetValue().Guid;
        }

        const auto saveResult = SaveProjectDefinitionToFile(GetProjectFilePathForRoot(projectRoot), definition);
        if (saveResult.IsFailure())
        {
            return saveResult;
        }

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Definition = definition;
        }

        LT_CORE_INFO("Project default scene set: key='{}' guid='{}'",
                     definition.DefaultScene.Key,
                     definition.DefaultScene.Guid);
        return Result<void>();
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

