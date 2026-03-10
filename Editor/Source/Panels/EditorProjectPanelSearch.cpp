#include "EditorProjectPanelShared.h"

namespace Limitless::EditorProjectPanel::Internal
{
    void SetProjectSearchFilter(EditorProjectPanelState& state, std::string value)
    {
        state.SearchFilterLower = ToLowerAscii(std::move(value));
    }

    const std::string& GetProjectSearchFilterLower(const EditorProjectPanelState& state)
    {
        return state.SearchFilterLower;
    }

    bool HasProjectSearchFilter(const EditorProjectPanelState& state)
    {
        return !state.SearchFilterLower.empty();
    }

    void ClearProjectSearchMatchCache(EditorProjectPanelState& state)
    {
        state.SearchMatchCache.clear();
    }

    bool MatchesProjectSearchFilter(const EditorProjectPanelState& state, const std::string& value)
    {
        if (state.SearchFilterLower.empty())
            return true;
        return ToLowerAscii(value).find(state.SearchFilterLower) != std::string::npos;
    }

    bool EntryMatchesProjectSearchFilter(const EditorProjectPanelState& state, const ProjectAssetTreeEntry& entry, const std::string& displayName)
    {
        if (state.SearchFilterLower.empty())
            return true;

        return MatchesProjectSearchFilter(state, displayName) ||
               MatchesProjectSearchFilter(state, entry.FileName) ||
               MatchesProjectSearchFilter(state, entry.AssetKey);
    }

    bool DirectoryContainsProjectSearchMatch(EditorProjectPanelState& state,
                                             const std::filesystem::path& assetsDirectory,
                                             const std::filesystem::path& relativePath)
    {
        if (state.SearchFilterLower.empty())
            return true;

        const std::string cacheKey = relativePath.generic_string();
        if (const auto cachedIt = state.SearchMatchCache.find(cacheKey); cachedIt != state.SearchMatchCache.end())
            return cachedIt->second;

        const std::vector<ProjectAssetTreeEntry>& entries = GetCachedProjectDirectoryEntries(state, assetsDirectory, relativePath);
        for (const ProjectAssetTreeEntry& entry : entries)
        {
            if (entry.IsDirectory)
            {
                if (MatchesProjectSearchFilter(state, entry.FileName) || DirectoryContainsProjectSearchMatch(state, assetsDirectory, entry.RelativePath))
                {
                    state.SearchMatchCache[cacheKey] = true;
                    return true;
                }
                continue;
            }

            const std::string displayName = IsScriptAssetExtensionLower(entry.LowerExtension)
                ? BuildProjectScriptAssetDisplayName(entry.AbsolutePath)
                : GetAssetDisplayName(entry.AbsolutePath);
            if (EntryMatchesProjectSearchFilter(state, entry, displayName))
            {
                state.SearchMatchCache[cacheKey] = true;
                return true;
            }
        }

        state.SearchMatchCache[cacheKey] = false;
        return false;
    }
}
