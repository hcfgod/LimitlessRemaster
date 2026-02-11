#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetLoadProgress
    // Central registry for asset load progress. Loaders report progress during async
    // operations; UI can query progress for display (e.g. loading indicators).
    //
    // Usage:
    // - Loaders call SetProgress(key, progress, status) at key stages.
    // - Call ClearProgress(key) when load completes or fails.
    // - UI calls GetProgress(key) to display; returns nullopt when complete.
    // -----------------------------------------------------------------------------
    class AssetLoadProgress final
    {
    public:
        struct Info
        {
            float Progress = 0.0f; ///< 0.0 to 1.0
            std::string Status;    ///< Human-readable stage (e.g. "Compiling...")
        };

        /// Report progress for an asset. Overwrites previous.
        static void SetProgress(const std::string& key, float progress, const std::string& status = "");

        /// Clear progress when load completes or fails. Removes from registry.
        static void ClearProgress(const std::string& key);

        /// Get current progress if the asset is still loading. Returns nullopt when complete.
        static std::optional<Info> GetProgress(const std::string& key);

        /// Get all asset keys currently being loaded. Useful for generic loading UIs.
        static std::vector<std::string> GetActiveKeys();

    private:
        static std::mutex s_Mutex;
        static std::unordered_map<std::string, Info> s_Progress;
    };
}
