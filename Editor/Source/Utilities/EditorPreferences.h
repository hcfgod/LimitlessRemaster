#pragma once

#include <filesystem>
#include <mutex>

namespace Limitless::Editor
{
    struct EditorPreferencesData final
    {
        float EntityFocusSinglePressDistanceMultiplier = 5.0f;
        float EntityFocusDoublePressDistanceMultiplier = 2.0f;
    };

    class EditorPreferences final
    {
    public:
        static EditorPreferences& GetInstance();

        void EnsureLoaded();
        EditorPreferencesData GetData() const;
        void SetData(const EditorPreferencesData& data);
        [[nodiscard]] std::filesystem::path GetStoragePath() const;

    private:
        EditorPreferences() = default;

        static EditorPreferencesData Normalize(EditorPreferencesData data);
        void LoadFromDiskUnlocked();
        void SaveToDiskUnlocked() const;

    private:
        mutable std::mutex m_Mutex;
        bool m_Loaded = false;
        EditorPreferencesData m_Data{};
    };
}
