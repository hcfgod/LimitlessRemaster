#pragma once

#include "EditorProjectPanel.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Limitless::EditorProjectPanel
{
    void InvalidateProjectDirectoryCache();
    void CopyTextToBuffer(std::array<char, 256>& destination, const char* source);

    std::string SanitizeScriptClassBaseName(std::string value);
    std::string SanitizeManagedScriptClassName(std::string value);

    bool CreateNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                        const std::filesystem::path& parentRelativePath,
                                        const std::string& requestedClassName,
                                        std::string& outCreatedSourceAssetKey,
                                        std::string& outError);

    bool CreateManagedScriptInAssets(const std::filesystem::path& assetsDirectory,
                                     const std::filesystem::path& parentRelativePath,
                                     const std::string& requestedClassName,
                                     std::string& outCreatedAssetKey,
                                     std::string& outError);

    bool RenameNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                        const std::filesystem::path& scriptRelativePath,
                                        const std::string& newDisplayName,
                                        std::filesystem::path& outNewHeaderRelativePath,
                                        std::filesystem::path& outNewSourceRelativePath);

    bool DeleteNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                        const std::filesystem::path& scriptRelativePath);

    void DrawProjectFolderPopups(const std::filesystem::path& assetsDirectory,
                                 EditorProjectPanelState& state,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateInputActionsRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
                                 const std::function<void(const std::string&, const std::string&)>& onAssetRenamed);
}
