#pragma once

#include <filesystem>
#include <string>

namespace Limitless::ProjectAssetOperations
{
    bool CreateFolderInDirectory(const std::filesystem::path& assetsDirectory,
                                 const std::filesystem::path& parentRelativePath,
                                 const std::string& folderName);

    bool DeleteFolderInAssets(const std::filesystem::path& assetsDirectory,
                              const std::filesystem::path& folderRelativePath);

    bool RenameFolderInAssets(const std::filesystem::path& assetsDirectory,
                              const std::filesystem::path& folderRelativePath,
                              const std::string& newName);

    bool MoveFolderToFolder(const std::string& folderAssetKey,
                            const std::filesystem::path& destinationFolderRelativePath);

    bool MoveAssetToFolder(const std::string& assetKey,
                           const std::filesystem::path& destinationFolderRelativePath);
}
