#include "Assets/AssetLoadProgress.h"

#include <mutex>
#include <unordered_map>

namespace Limitless::Assets
{
    std::mutex AssetLoadProgress::s_Mutex;
    std::unordered_map<std::string, AssetLoadProgress::Info> AssetLoadProgress::s_Progress;

    void AssetLoadProgress::SetProgress(const std::string& key, float progress, const std::string& status)
    {
        if (key.empty())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(s_Mutex);
        Info& info = s_Progress[key];
        info.Progress = progress;
        info.Status = status;
    }

    void AssetLoadProgress::ClearProgress(const std::string& key)
    {
        if (key.empty())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Progress.erase(key);
    }

    std::optional<AssetLoadProgress::Info> AssetLoadProgress::GetProgress(const std::string& key)
    {
        if (key.empty())
        {
            return std::nullopt;
        }
        std::lock_guard<std::mutex> lock(s_Mutex);
        const auto it = s_Progress.find(key);
        if (it == s_Progress.end())
        {
            return std::nullopt;
        }
        return it->second;
    }
}
