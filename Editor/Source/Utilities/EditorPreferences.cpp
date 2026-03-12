#include "EditorPreferences.h"

#include "Core/Debug/Log.h"
#include "Platform/Platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace Limitless::Editor
{
    namespace
    {
        using json = nlohmann::json;

        constexpr uint32_t kEditorPreferencesVersion = 1;

        std::filesystem::path ResolveFallbackUserDataPath()
        {
#if defined(LT_PLATFORM_WINDOWS)
            if (const char* appData = std::getenv("APPDATA"); appData && appData[0] != '\0')
                return std::filesystem::path(appData) / "Limitless";
#else
            if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
                return std::filesystem::path(home) / ".local" / "share" / "Limitless";
#endif
            return {};
        }
    }

    EditorPreferences& EditorPreferences::GetInstance()
    {
        static EditorPreferences s_Instance;
        return s_Instance;
    }

    void EditorPreferences::EnsureLoaded()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Loaded)
            return;
        m_Loaded = true;
        LoadFromDiskUnlocked();
    }

    EditorPreferencesData EditorPreferences::GetData() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Data;
    }

    void EditorPreferences::SetData(const EditorPreferencesData& data)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Data = Normalize(data);
        m_Loaded = true;
        SaveToDiskUnlocked();
    }

    std::filesystem::path EditorPreferences::GetStoragePath() const
    {
        const std::string userDataPath = PlatformDetection::GetUserDataPath();
        std::filesystem::path root = userDataPath.empty()
            ? ResolveFallbackUserDataPath()
            : std::filesystem::path(userDataPath);
        if (root.empty())
            return {};
        return root / "Editor" / "Preferences.json";
    }

    EditorPreferencesData EditorPreferences::Normalize(EditorPreferencesData data)
    {
        data.EntityFocusSinglePressDistanceMultiplier = std::clamp(data.EntityFocusSinglePressDistanceMultiplier, 1.0f, 12.0f);
        data.EntityFocusDoublePressDistanceMultiplier = std::clamp(data.EntityFocusDoublePressDistanceMultiplier, 0.5f, 8.0f);
        if (data.EntityFocusDoublePressDistanceMultiplier > data.EntityFocusSinglePressDistanceMultiplier)
            data.EntityFocusDoublePressDistanceMultiplier = data.EntityFocusSinglePressDistanceMultiplier;
        return data;
    }

    void EditorPreferences::LoadFromDiskUnlocked()
    {
        m_Data = Normalize(EditorPreferencesData{});
        const std::filesystem::path storagePath = GetStoragePath();
        if (storagePath.empty())
            return;

        std::error_code ec;
        if (!std::filesystem::exists(storagePath, ec))
            return;

        try
        {
            std::ifstream in(storagePath, std::ios::in | std::ios::binary);
            if (!in.is_open())
                return;

            json root;
            in >> root;
            if (!root.is_object())
                return;
            if (root.value("version", 0u) != kEditorPreferencesVersion)
                return;

            EditorPreferencesData loaded = m_Data;
            loaded.EntityFocusSinglePressDistanceMultiplier = root.value("entityFocusSinglePressDistanceMultiplier", loaded.EntityFocusSinglePressDistanceMultiplier);
            loaded.EntityFocusDoublePressDistanceMultiplier = root.value("entityFocusDoublePressDistanceMultiplier", loaded.EntityFocusDoublePressDistanceMultiplier);
            m_Data = Normalize(loaded);
        }
        catch (...)
        {
        }
    }

    void EditorPreferences::SaveToDiskUnlocked() const
    {
        const std::filesystem::path storagePath = GetStoragePath();
        if (storagePath.empty())
            return;

        try
        {
            std::error_code ec;
            std::filesystem::create_directories(storagePath.parent_path(), ec);
            if (ec)
                return;

            json root;
            root["version"] = kEditorPreferencesVersion;
            root["entityFocusSinglePressDistanceMultiplier"] = m_Data.EntityFocusSinglePressDistanceMultiplier;
            root["entityFocusDoublePressDistanceMultiplier"] = m_Data.EntityFocusDoublePressDistanceMultiplier;

            const std::filesystem::path tmpPath = storagePath.string() + ".tmp";
            {
                std::ofstream out(tmpPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                    return;
                out << root.dump(2);
                out.flush();
            }

            std::filesystem::rename(tmpPath, storagePath, ec);
            if (ec)
            {
                ec.clear();
                std::filesystem::remove(storagePath, ec);
                ec.clear();
                std::filesystem::rename(tmpPath, storagePath, ec);
            }
        }
        catch (...)
        {
        }
    }
}
