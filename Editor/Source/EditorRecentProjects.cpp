#include "EditorRecentProjects.h"

#include "Core/Debug/Log.h"
#include "Platform/Platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Limitless::Editor
{
    using json = nlohmann::json;

    static constexpr uint32_t kRecentProjectsVersion = 1;

    static std::string NormalizePathForStorage(const std::filesystem::path& p)
    {
        // Prefer forward slashes for portable JSON storage.
        return p.lexically_normal().generic_string();
    }

    static std::filesystem::path ResolveFallbackUserDataPath()
    {
#if defined(LT_PLATFORM_WINDOWS)
        if (const char* appData = std::getenv("APPDATA"); appData && appData[0] != '\0')
        {
            return std::filesystem::path(appData) / "Limitless";
        }
#else
        if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
        {
            return std::filesystem::path(home) / ".local" / "share" / "Limitless";
        }
#endif
        return {};
    }

    std::string EditorRecentProjects::UtcNowIso8601()
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

    EditorRecentProjects& EditorRecentProjects::GetInstance()
    {
        static EditorRecentProjects s_Instance;
        return s_Instance;
    }

    std::filesystem::path EditorRecentProjects::GetStoragePath() const
    {
        const std::string userDataPath = PlatformDetection::GetUserDataPath();
        std::filesystem::path p = userDataPath.empty()
            ? ResolveFallbackUserDataPath()
            : std::filesystem::path(userDataPath);
        if (p.empty())
            return {};
        p /= "Editor";
        p /= "RecentProjects.json";
        return p;
    }

    void EditorRecentProjects::EnsureLoaded()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Loaded)
        {
            return;
        }

        const std::filesystem::path storage = GetStoragePath();
        if (storage.empty())
        {
            return;
        }

        m_Loaded = true;
        m_Entries.clear();

        if (!std::filesystem::exists(storage))
        {
            return;
        }

        try
        {
            std::ifstream in(storage, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                return;
            }

            json root;
            in >> root;
            if (!root.is_object())
            {
                return;
            }

            const uint32_t version = root.value("version", 0u);
            if (version != kRecentProjectsVersion)
            {
                return;
            }

            if (!root.contains("projects") || !root["projects"].is_array())
            {
                return;
            }

            for (const auto& entry : root["projects"])
            {
                if (!entry.is_object())
                {
                    continue;
                }

                RecentProjectEntry e;
                e.ProjectRoot = entry.value("projectRoot", std::string{});
                e.ProjectName = entry.value("projectName", std::string{});
                e.LastOpenedUtc = entry.value("lastOpenedUtc", std::string{});
                if (e.ProjectRoot.empty())
                {
                    continue;
                }
                m_Entries.push_back(std::move(e));
            }
        }
        catch (const std::exception& e)
        {
            LT_CORE_WARN("EditorRecentProjects: failed to load: {}", e.what());
        }
    }

    void EditorRecentProjects::SaveToDiskBestEffort() const
    {
        const std::filesystem::path storage = GetStoragePath();
        if (storage.empty())
        {
            return;
        }

        try
        {
            std::error_code ec;
            std::filesystem::create_directories(storage.parent_path(), ec);

            json root;
            root["version"] = kRecentProjectsVersion;
            root["projects"] = json::array();
            for (const auto& e : m_Entries)
            {
                json j;
                j["projectRoot"] = e.ProjectRoot;
                j["projectName"] = e.ProjectName;
                j["lastOpenedUtc"] = e.LastOpenedUtc;
                root["projects"].push_back(std::move(j));
            }

            const std::filesystem::path tmp = storage.string() + ".tmp";
            {
                std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                {
                    return;
                }
                out << root.dump(2);
                out.flush();
            }

            std::filesystem::rename(tmp, storage, ec);
            if (ec)
            {
                // Windows replacement path.
                ec.clear();
                std::filesystem::remove(storage, ec);
                ec.clear();
                std::filesystem::rename(tmp, storage, ec);
            }
        }
        catch (...)
        {
            // Best-effort persistence only.
        }
    }

    void EditorRecentProjects::AddOrUpdate(const std::filesystem::path& projectRoot, const std::string& projectName)
    {
        EnsureLoaded();

        std::error_code ec;
        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(projectRoot, ec);
        const std::filesystem::path normalizedRootPath = ec ? projectRoot.lexically_normal() : canonicalRoot;
        const std::string normalized = NormalizePathForStorage(normalizedRootPath);
        if (normalized.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const RecentProjectEntry& e) {
            return e.ProjectRoot == normalized;
        });

        if (it == m_Entries.end())
        {
            RecentProjectEntry e;
            e.ProjectRoot = normalized;
            e.ProjectName = projectName;
            e.LastOpenedUtc = UtcNowIso8601();
            m_Entries.insert(m_Entries.begin(), std::move(e));
        }
        else
        {
            it->ProjectName = projectName;
            it->LastOpenedUtc = UtcNowIso8601();

            // Move to front.
            RecentProjectEntry moved = *it;
            m_Entries.erase(it);
            m_Entries.insert(m_Entries.begin(), std::move(moved));
        }

        // Keep list small and stable.
        if (m_Entries.size() > 20)
        {
            m_Entries.resize(20);
        }

        SaveToDiskBestEffort();
    }

    void EditorRecentProjects::Remove(const std::filesystem::path& projectRoot)
    {
        EnsureLoaded();

        std::error_code ec;
        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(projectRoot, ec);
        const std::filesystem::path normalizedRootPath = ec ? projectRoot.lexically_normal() : canonicalRoot;
        const std::string normalized = NormalizePathForStorage(normalizedRootPath);
        if (normalized.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Entries.erase(
            std::remove_if(m_Entries.begin(), m_Entries.end(), [&](const RecentProjectEntry& e) { return e.ProjectRoot == normalized; }),
            m_Entries.end());

        SaveToDiskBestEffort();
    }

    std::vector<RecentProjectEntry> EditorRecentProjects::GetEntries() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Entries;
    }
}

