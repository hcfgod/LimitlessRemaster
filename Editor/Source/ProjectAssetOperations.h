#pragma once

#include <filesystem>
#include <string>
#include <vector>

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

    // Import external files/folders from the host file system into Assets/<destinationFolderRelativePath>.
    // Returns true if at least one file/folder was copied successfully.
    bool ImportExternalPathsToFolder(const std::vector<std::filesystem::path>& sourcePaths,
                                     const std::filesystem::path& destinationFolderRelativePath);
}
