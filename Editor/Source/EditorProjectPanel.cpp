#include "EditorProjectPanel.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorProjectPanel
{
    namespace
    {
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";
        constexpr const char* kAssetMultiSelectionPayload = "ASSET_MULTI_KEYS";

        std::vector<std::string> ParseAssetKeyListPayload(const void* payloadData, int payloadSize)
        {
            std::vector<std::string> keys;
            if (!payloadData || payloadSize <= 0)
                return keys;

            std::string payloadText(static_cast<const char*>(payloadData), static_cast<size_t>(payloadSize));
            while (!payloadText.empty() && payloadText.back() == '\0')
                payloadText.pop_back();
            if (payloadText.empty())
                return keys;

            size_t lineStart = 0;
            while (lineStart < payloadText.size())
            {
                const size_t lineEnd = payloadText.find('\n', lineStart);
                const size_t count = (lineEnd == std::string::npos) ? (payloadText.size() - lineStart) : (lineEnd - lineStart);
                std::string key = payloadText.substr(lineStart, count);
                if (!key.empty())
                    keys.push_back(std::move(key));
                if (lineEnd == std::string::npos)
                    break;
                lineStart = lineEnd + 1;
            }

            return keys;
        }

        std::string EncodeAssetKeyListPayload(const std::vector<std::string>& keys)
        {
            std::string payloadText;
            for (const auto& key : keys)
            {
                if (key.empty())
                    continue;
                if (!payloadText.empty())
                    payloadText.push_back('\n');
                payloadText += key;
            }
            return payloadText;
        }
        constexpr uint8_t kScriptPairHeaderBit = 1u << 0u;
        constexpr uint8_t kScriptPairSourceBit = 1u << 1u;
        constexpr std::chrono::milliseconds kDirectoryCacheRefreshInterval(250);

        struct ProjectAssetTreeEntry
        {
            std::filesystem::path AbsolutePath;
            std::filesystem::path RelativePath;
            std::string FileName;
            std::string LowerFileName;
            std::string LowerExtension;
            std::string AssetKey;
            bool IsDirectory = false;
        };

        struct ProjectAssetDirectoryCacheEntry
        {
            std::vector<ProjectAssetTreeEntry> Entries;
            std::chrono::steady_clock::time_point LastRefreshTime = {};
        };

        std::unordered_map<std::string, ProjectAssetDirectoryCacheEntry> gProjectAssetDirectoryCache;

        std::string ToLowerAscii(std::string value)
        {
            for (char& character : value)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        bool IsTextureExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".png" || lowerExtension == ".jpg" || lowerExtension == ".jpeg" ||
                   lowerExtension == ".ppm" || lowerExtension == ".pnm" || lowerExtension == ".bmp" ||
                   lowerExtension == ".tga" || lowerExtension == ".gif";
        }

        bool IsSceneFileNameLower(const std::string& lowerFileName)
        {
            return lowerFileName.size() >= 11 && lowerFileName.rfind(".scene.json") == (lowerFileName.size() - 11);
        }

        bool IsMaterialFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* materialSuffix = ".material.json";
            const std::string suffixString = materialSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsTilesetFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* tilesetSuffix = ".tileset.json";
            const std::string suffixString = tilesetSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsAudioMixerFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* audioMixerSuffix = ".audiomixer.json";
            const std::string suffixString = audioMixerSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsInputActionsFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* inputActionsSuffix = ".inputactions.json";
            const std::string suffixString = inputActionsSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsPrefabFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* prefabSuffix = ".prefab.json";
            const std::string suffixString = prefabSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsAnimationClipFileNameLower(const std::string& lowerFileName)
        {
            return lowerFileName.ends_with(".animationclip.json") ||
                   lowerFileName.ends_with(".animation.json") ||
                   lowerFileName.ends_with(".anim.json");
        }

        bool IsAnimatorControllerFileNameLower(const std::string& lowerFileName)
        {
            return lowerFileName.ends_with(".animcontroller.json") ||
                   lowerFileName.ends_with(".animatorcontroller.json");
        }

        bool IsShaderExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".glsl";
        }

        bool IsAudioExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".wav" || lowerExtension == ".mp3" || lowerExtension == ".ogg" || lowerExtension == ".flac";
        }

        bool IsFontExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".ttf" || lowerExtension == ".otf";
        }

        bool IsNativeScriptExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".h" || lowerExtension == ".cpp";
        }

        std::vector<ProjectAssetTreeEntry> ScanProjectDirectoryEntries(const std::filesystem::path& assetsDirectory,
                                                                       const std::filesystem::path& relativePath)
        {
            std::vector<ProjectAssetTreeEntry> entries;
            const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
            std::error_code errorCode;
            for (const auto& entry : std::filesystem::directory_iterator(currentDirectory, errorCode))
            {
                if (errorCode)
                    continue;

                ProjectAssetTreeEntry nextEntry;
                nextEntry.AbsolutePath = entry.path();
                nextEntry.FileName = nextEntry.AbsolutePath.filename().string();
                if (nextEntry.FileName.empty() || nextEntry.FileName[0] == '.')
                    continue;
                if (nextEntry.FileName == "Cache")
                    continue;

                std::error_code isDirectoryError;
                nextEntry.IsDirectory = entry.is_directory(isDirectoryError);
                if (isDirectoryError)
                    nextEntry.IsDirectory = false;

                nextEntry.LowerFileName = ToLowerAscii(nextEntry.FileName);
                if (!nextEntry.IsDirectory)
                {
                    nextEntry.LowerExtension = ToLowerAscii(nextEntry.AbsolutePath.extension().string());
                    if (nextEntry.LowerExtension == ".meta")
                        continue;
                }

                nextEntry.RelativePath = relativePath / nextEntry.FileName;
                nextEntry.AssetKey = "Assets/" + nextEntry.RelativePath.generic_string();
                entries.push_back(std::move(nextEntry));
            }

            std::sort(entries.begin(), entries.end(), [](const ProjectAssetTreeEntry& left, const ProjectAssetTreeEntry& right) {
                if (left.IsDirectory != right.IsDirectory)
                    return left.IsDirectory;
                return left.FileName < right.FileName;
            });

            return entries;
        }

        const std::vector<ProjectAssetTreeEntry>& GetCachedProjectDirectoryEntries(const std::filesystem::path& assetsDirectory,
                                                                                    const std::filesystem::path& relativePath)
        {
            const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
            const std::string cacheKey = currentDirectory.lexically_normal().generic_string();
            ProjectAssetDirectoryCacheEntry& cacheEntry = gProjectAssetDirectoryCache[cacheKey];

            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            const bool shouldRefresh = cacheEntry.Entries.empty() ||
                (now - cacheEntry.LastRefreshTime) >= kDirectoryCacheRefreshInterval;
            if (shouldRefresh)
            {
                cacheEntry.Entries = ScanProjectDirectoryEntries(assetsDirectory, relativePath);
                cacheEntry.LastRefreshTime = now;
            }

            return cacheEntry.Entries;
        }

        void InvalidateProjectDirectoryCache()
        {
            gProjectAssetDirectoryCache.clear();
        }

        void MoveAssetOrFolderToTargetFolder(const char* assetOrFolderKey,
                                             const std::filesystem::path& destinationFolderRelativePath)
        {
            if (!assetOrFolderKey || !assetOrFolderKey[0])
                return;

            const bool moved = std::filesystem::path(assetOrFolderKey).extension().empty()
                ? ProjectAssetOperations::MoveFolderToFolder(assetOrFolderKey, destinationFolderRelativePath)
                : ProjectAssetOperations::MoveAssetToFolder(assetOrFolderKey, destinationFolderRelativePath);
            if (moved)
                InvalidateProjectDirectoryCache();
        }

        void MoveAssetListToTargetFolder(const std::vector<std::string>& assetKeys,
                                         const std::filesystem::path& destinationFolderRelativePath)
        {
            bool movedAny = false;
            for (const auto& key : assetKeys)
            {
                if (key.empty())
                    continue;
                const bool moved = std::filesystem::path(key).extension().empty()
                    ? ProjectAssetOperations::MoveFolderToFolder(key.c_str(), destinationFolderRelativePath)
                    : ProjectAssetOperations::MoveAssetToFolder(key.c_str(), destinationFolderRelativePath);
                movedAny |= moved;
            }

            if (movedAny)
                InvalidateProjectDirectoryCache();
        }

        void CopyTextToBuffer(std::array<char, 256>& destination, const char* source)
        {
            if (!source)
            {
                destination[0] = '\0';
                return;
            }

            std::snprintf(destination.data(), destination.size(), "%s", source);
        }

        std::string BuildScriptAssetDisplayName(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            const std::string stem = path.stem().string();
            if (extension == ".h")
                return stem + " [.h Header]";
            return stem + " [.cpp Source]";
        }

        std::string SanitizeScriptClassBaseName(std::string value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
                value.erase(value.begin());
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
                value.pop_back();

            std::string sanitized;
            sanitized.reserve(value.size() + 8);
            for (char character : value)
            {
                const unsigned char raw = static_cast<unsigned char>(character);
                if (std::isalnum(raw) || character == '_')
                    sanitized.push_back(character);
            }

            if (sanitized.empty())
                sanitized = "NewNativeScript";
            if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0)
                sanitized.insert(0, "Script_");
            return sanitized;
        }

        bool CreateNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                            const std::filesystem::path& parentRelativePath,
                                            const std::string& requestedClassName,
                                            std::string& outCreatedSourceAssetKey,
                                            std::string& outError)
        {
            const std::string className = SanitizeScriptClassBaseName(requestedClassName);
            const std::filesystem::path scriptDirectory = assetsDirectory / parentRelativePath;

            std::error_code errorCode;
            std::filesystem::create_directories(scriptDirectory, errorCode);
            if (errorCode)
            {
                outError = "Failed to create script directory: " + errorCode.message();
                return false;
            }

            const std::filesystem::path headerPath = scriptDirectory / (className + ".h");
            const std::filesystem::path sourcePath = scriptDirectory / (className + ".cpp");
            if (std::filesystem::exists(headerPath) || std::filesystem::exists(sourcePath))
            {
                outError = "Script already exists: " + className;
                return false;
            }

            const std::string headerTemplate =
                "#pragma once\n\n"
                "#include \"Limitless.h\"\n\n"
                "class " + className + " final : public Limitless::ScriptableEntity\n"
                "{\n"
                "public:\n"
                "    float RotationSpeed = 90.0f;\n\n"
                "protected:\n"
                "    LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()\n"
                "        LT_AUTO_EXPOSED_FIELD(RotationSpeed)\n"
                "    LT_END_AUTO_EXPOSED_FIELD_SYNC()\n\n"
                "    void OnCreate() override;\n"
                "    void OnUpdate(float deltaTime) override;\n"
                "    void OnDestroy() override;\n"
                "};\n";

            const std::string sourceTemplate =
                "#include \"" + className + ".h\"\n\n"
                "#include \"ScriptCoreRegistration.h\"\n\n"
                "void " + className + "::OnCreate()\n"
                "{\n"
                "}\n\n"
                "void " + className + "::OnUpdate(float deltaTime)\n"
                "{\n"
                "    auto& transform = GetComponent<Limitless::TransformComponent>();\n"
                "    transform.Rotation.z += RotationSpeed * deltaTime;\n"
                "    if (transform.Rotation.z > 360.0f)\n"
                "        transform.Rotation.z -= 360.0f;\n"
                "}\n\n"
                "void " + className + "::OnDestroy()\n"
                "{\n"
                "}\n\n"
                "LT_REGISTER_SCRIPTCORE_SCRIPT(" + className + ");\n";

            {
                std::ofstream headerOutput(headerPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!headerOutput.is_open())
                {
                    outError = "Failed to create header file: " + headerPath.string();
                    return false;
                }
                headerOutput << headerTemplate;
            }

            {
                std::ofstream sourceOutput(sourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!sourceOutput.is_open())
                {
                    outError = "Failed to create source file: " + sourcePath.string();
                    return false;
                }
                sourceOutput << sourceTemplate;
            }

            (void)Assets::AssetImportPipeline::ReimportChanged(true);
            outCreatedSourceAssetKey = "Assets/" + (parentRelativePath / (className + ".cpp")).generic_string();
            outError.clear();
            InvalidateProjectDirectoryCache();
            return true;
        }

        std::string EscapeRegexLiteral(const std::string& value)
        {
            std::string escaped;
            escaped.reserve(value.size() * 2);
            for (char character : value)
            {
                switch (character)
                {
                    case '.': case '^': case '$': case '|': case '(': case ')':
                    case '[': case ']': case '{': case '}': case '*': case '+':
                    case '?': case '\\':
                        escaped.push_back('\\');
                        break;
                    default:
                        break;
                }
                escaped.push_back(character);
            }
            return escaped;
        }

        bool ReplaceWholeWordInFile(const std::filesystem::path& filePath,
                                    const std::string& oldWord,
                                    const std::string& newWord)
        {
            std::ifstream input(filePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();

            const std::regex wholeWordPattern("\\b" + EscapeRegexLiteral(oldWord) + "\\b");
            content = std::regex_replace(content, wholeWordPattern, newWord);

            std::ofstream output(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << content;
            return output.good();
        }

        bool RewriteSourceIncludeForRenamedScriptPair(const std::filesystem::path& sourcePath,
                                                      const std::string& oldHeaderName,
                                                      const std::string& newHeaderName)
        {
            std::ifstream input(sourcePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();

            const std::string oldInclude = "#include \"" + oldHeaderName + "\"";
            const std::string newInclude = "#include \"" + newHeaderName + "\"";
            const size_t includePosition = content.find(oldInclude);
            if (includePosition == std::string::npos)
                return true;
            content.replace(includePosition, oldInclude.size(), newInclude);

            std::ofstream output(sourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << content;
            return output.good();
        }

        bool RenameNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                            const std::filesystem::path& scriptRelativePath,
                                            const std::string& newDisplayName,
                                            std::filesystem::path& outNewHeaderRelativePath,
                                            std::filesystem::path& outNewSourceRelativePath)
        {
            const std::string sanitizedBaseName = SanitizeScriptClassBaseName(newDisplayName);

            const std::filesystem::path baseRelativePath = scriptRelativePath.parent_path() / scriptRelativePath.stem();
            const std::string oldClassName = baseRelativePath.stem().string();
            const std::string newClassName = sanitizedBaseName;
            const std::filesystem::path headerRelativePath = baseRelativePath.string() + ".h";
            const std::filesystem::path sourceRelativePath = baseRelativePath.string() + ".cpp";
            const std::filesystem::path headerPath = assetsDirectory / headerRelativePath;
            const std::filesystem::path sourcePath = assetsDirectory / sourceRelativePath;

            std::error_code errorCode;
            if (!std::filesystem::exists(headerPath, errorCode) || !std::filesystem::exists(sourcePath, errorCode))
                return false;

            const std::filesystem::path newBaseRelativePath = baseRelativePath.parent_path() / sanitizedBaseName;
            outNewHeaderRelativePath = newBaseRelativePath.string() + ".h";
            outNewSourceRelativePath = newBaseRelativePath.string() + ".cpp";
            const std::filesystem::path newHeaderPath = assetsDirectory / outNewHeaderRelativePath;
            const std::filesystem::path newSourcePath = assetsDirectory / outNewSourceRelativePath;

            if (newHeaderPath == headerPath && newSourcePath == sourcePath)
                return true;
            if (std::filesystem::exists(newHeaderPath, errorCode) || std::filesystem::exists(newSourcePath, errorCode))
                return false;

            std::filesystem::rename(headerPath, newHeaderPath, errorCode);
            if (errorCode)
                return false;
            std::filesystem::rename(sourcePath, newSourcePath, errorCode);
            if (errorCode)
                return false;

            const std::filesystem::path oldHeaderMetaPath = headerPath.parent_path() / (headerPath.filename().string() + ".meta");
            const std::filesystem::path oldSourceMetaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
            const std::filesystem::path newHeaderMetaPath = newHeaderPath.parent_path() / (newHeaderPath.filename().string() + ".meta");
            const std::filesystem::path newSourceMetaPath = newSourcePath.parent_path() / (newSourcePath.filename().string() + ".meta");
            if (std::filesystem::exists(oldHeaderMetaPath, errorCode))
                std::filesystem::rename(oldHeaderMetaPath, newHeaderMetaPath, errorCode);
            errorCode.clear();
            if (std::filesystem::exists(oldSourceMetaPath, errorCode))
                std::filesystem::rename(oldSourceMetaPath, newSourceMetaPath, errorCode);

            (void)RewriteSourceIncludeForRenamedScriptPair(newSourcePath, headerPath.filename().string(), newHeaderPath.filename().string());
            (void)ReplaceWholeWordInFile(newHeaderPath, oldClassName, newClassName);
            (void)ReplaceWholeWordInFile(newSourcePath, oldClassName, newClassName);
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
            InvalidateProjectDirectoryCache();
            return true;
        }

        bool DeleteNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory, const std::filesystem::path& scriptRelativePath)
        {
            const std::filesystem::path baseRelativePath = scriptRelativePath.parent_path() / scriptRelativePath.stem();
            const std::filesystem::path headerPath = assetsDirectory / (baseRelativePath.string() + ".h");
            const std::filesystem::path sourcePath = assetsDirectory / (baseRelativePath.string() + ".cpp");

            std::error_code errorCode;
            bool removedAny = false;
            if (std::filesystem::exists(headerPath, errorCode))
                removedAny |= std::filesystem::remove(headerPath, errorCode);
            errorCode.clear();
            if (std::filesystem::exists(sourcePath, errorCode))
                removedAny |= std::filesystem::remove(sourcePath, errorCode);
            errorCode.clear();

            const std::filesystem::path headerMetaPath = headerPath.parent_path() / (headerPath.filename().string() + ".meta");
            const std::filesystem::path sourceMetaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
            std::filesystem::remove(headerMetaPath, errorCode);
            errorCode.clear();
            std::filesystem::remove(sourceMetaPath, errorCode);

            if (removedAny)
            {
                (void)Assets::AssetImportPipeline::ReimportChanged(true);
                InvalidateProjectDirectoryCache();
            }
            return removedAny;
        }

        std::string GetAssetDisplayName(const std::filesystem::path& path)
        {
            return EditorAssetNaming::GetAssetDisplayNameFromPath(path);
        }

        void DrawAssetTree(const std::filesystem::path& assetsDirectory,
                           const std::filesystem::path& relativePath,
                           EditorProjectPanelState& state,
                           entt::entity& selectedEntity,
                           std::string& selectedTextureAssetKey,
                           Assets::TextureAsset::Ptr& cachedTextureAsset,
                           std::string& selectedMaterialAssetKey,
                           Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                           std::string& selectedNativeScriptAssetKey,
                           std::string& selectedPrefabAssetKey,
                           std::string& selectedTilesetAssetKey,
                           std::string& selectedAudioMixerAssetKey,
                           std::string& selectedInputActionsAssetKey,
                          std::string& selectedAnimationClipAssetKey,
                          std::string& selectedAnimatorControllerAssetKey,
                           const char* texturePayloadId,
                           const char* audioPayloadId,
                           const char* assetMovePayloadId,
                           const char* scenePayloadId,
                           const char* materialPayloadId,
                           const char* prefabPayloadId,
                           const char* shaderPayloadId,
                           const char* fontPayloadId,
                           const std::function<void(const std::string&)>& onSceneActivated,
                           const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested,
                           const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
                           const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
                           const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
                          const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
                          const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
                           const std::function<void(entt::entity, const std::filesystem::path&)>& onCreatePrefabFromSceneEntityRequested,
                           const std::function<void(const std::string&)>& onPrefabOpened,
                           const std::function<void(const std::string&)>& onPrefabInstantiated,
                           const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
                           const std::function<void(const std::string&, const std::string&)>& onAssetRenamed,
                           const std::function<void(const std::string&)>& onNativeScriptAssetActivated)
        {
            const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
            std::error_code errorCode;
            if (!std::filesystem::exists(currentDirectory, errorCode) || !std::filesystem::is_directory(currentDirectory, errorCode))
                return;

            const std::vector<ProjectAssetTreeEntry>& entries = GetCachedProjectDirectoryEntries(assetsDirectory, relativePath);
            std::vector<std::string> directoryAssetKeys;
            directoryAssetKeys.reserve(entries.size());
            for (const auto& entry : entries)
            {
                if (!entry.IsDirectory && !entry.AssetKey.empty())
                    directoryAssetKeys.push_back(entry.AssetKey);
            }

            std::unordered_map<std::string, uint8_t> scriptPairPresenceByBasePath;
            scriptPairPresenceByBasePath.reserve(entries.size());
            for (const ProjectAssetTreeEntry& entry : entries)
            {
                if (entry.IsDirectory || !IsNativeScriptExtensionLower(entry.LowerExtension))
                    continue;

                const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / std::filesystem::path(entry.FileName).stem();
                const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                uint8_t& presenceBits = scriptPairPresenceByBasePath[scriptBaseKey];
                if (entry.LowerExtension == ".h")
                    presenceBits |= kScriptPairHeaderBit;
                else if (entry.LowerExtension == ".cpp")
                    presenceBits |= kScriptPairSourceBit;
            }

            std::unordered_set<std::string> renderedScriptBasePaths;
            for (const ProjectAssetTreeEntry& entry : entries)
            {
                const std::string& fileName = entry.FileName;
                const bool isDirectory = entry.IsDirectory;
                const std::string& assetKey = entry.AssetKey;
                const std::filesystem::path& entryRelativePath = entry.RelativePath;

                if (isDirectory)
                {
                    const bool nodeOpen = ImGui::TreeNodeEx(fileName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
                    {
                        state.HoveredFolderRelativePathForExternalDrop = entryRelativePath;
                    }

                    if (ImGui::BeginPopupContextItem())
                    {
                        state.FolderPopupParent = entryRelativePath;
                        if (ImGui::MenuItem("Create Folder"))
                        {
                            state.FolderPopupPending = EditorProjectFolderPopup::Create;
                            CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
                        }
                        if (ImGui::MenuItem("Create Scene") && onCreateSceneRequested)
                            onCreateSceneRequested(entryRelativePath);
                        if (ImGui::MenuItem("Create Material"))
                        {
                            state.CreateMaterialParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateMaterialNameBuffer, "New Material");
                            state.CreateMaterialPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Tileset"))
                        {
                            state.CreateTilesetParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateTilesetNameBuffer, "New Tileset");
                            state.CreateTilesetPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Audio Mixer"))
                        {
                            state.CreateAudioMixerParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateAudioMixerNameBuffer, "New Audio Mixer");
                            state.CreateAudioMixerPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Animation Clip"))
                        {
                            state.CreateAnimationClipParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateAnimationClipNameBuffer, "New Animation Clip");
                            state.CreateAnimationClipPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Animator Controller"))
                        {
                            state.CreateAnimatorControllerParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateAnimatorControllerNameBuffer, "New Animator Controller");
                            state.CreateAnimatorControllerPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Native Script"))
                        {
                            state.CreateNativeScriptParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateNativeScriptClassNameBuffer, "NewNativeScript");
                            state.CreateNativeScriptPopupPending = true;
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            state.FolderPopupPending = EditorProjectFolderPopup::Rename;
                            CopyTextToBuffer(state.FolderPopupBuffer, fileName.c_str());
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete"))
                        {
                            if (ProjectAssetOperations::DeleteFolderInAssets(assetsDirectory, entryRelativePath))
                            {
                                InvalidateProjectDirectoryCache();
                                LT_INFO("Deleted folder {}", entryRelativePath.generic_string());
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMultiSelectionPayload))
                        {
                            const std::vector<std::string> keys = ParseAssetKeyListPayload(payload->Data, payload->DataSize);
                            MoveAssetListToTargetFolder(keys, entryRelativePath);
                        }
                        else
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                        {
                            const auto* entity = static_cast<const entt::entity*>(payload->Data);
                            if (entity && onCreatePrefabFromSceneEntityRequested)
                                onCreatePrefabFromSceneEntityRequested(*entity, entryRelativePath);
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        ImGui::Text("%s", fileName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (nodeOpen)
                    {
                        DrawAssetTree(assetsDirectory,
                                      entryRelativePath,
                                      state,
                                      selectedEntity,
                                      selectedTextureAssetKey,
                                      cachedTextureAsset,
                                      selectedMaterialAssetKey,
                                      cachedMaterialAsset,
                                      selectedNativeScriptAssetKey,
                                      selectedPrefabAssetKey,
                                      selectedTilesetAssetKey,
                                      selectedAudioMixerAssetKey,
                                      selectedInputActionsAssetKey,
                                      selectedAnimationClipAssetKey,
                                      selectedAnimatorControllerAssetKey,
                                      texturePayloadId,
                                      audioPayloadId,
                                      assetMovePayloadId,
                                      scenePayloadId,
                                      materialPayloadId,
                                      prefabPayloadId,
                                      shaderPayloadId,
                                      fontPayloadId,
                                      onSceneActivated,
                                      onCreateSceneRequested,
                                      onCreateMaterialRequested,
                                      onCreateTilesetRequested,
                                      onCreateAudioMixerRequested,
                                      onCreateAnimationClipRequested,
                                      onCreateAnimatorControllerRequested,
                                      onCreatePrefabFromSceneEntityRequested,
                                      onPrefabOpened,
                                      onPrefabInstantiated,
                                      onSetDefaultSceneRequested,
                                      onAssetRenamed,
                                      onNativeScriptAssetActivated);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    if (IsNativeScriptExtensionLower(entry.LowerExtension))
                    {
                        const std::filesystem::path scriptBaseRelativePath = entryRelativePath.parent_path() / entryRelativePath.stem();
                        const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                        if (renderedScriptBasePaths.find(scriptBaseKey) != renderedScriptBasePaths.end())
                            continue;

                        const std::filesystem::path headerRelativePath = scriptBaseRelativePath.string() + ".h";
                        const std::filesystem::path sourceRelativePath = scriptBaseRelativePath.string() + ".cpp";
                        const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseKey);
                        const bool hasScriptPair =
                            scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                            (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                (kScriptPairHeaderBit | kScriptPairSourceBit);
                        if (hasScriptPair)
                        {
                            renderedScriptBasePaths.insert(scriptBaseKey);
                            const std::string scriptBaseName = scriptBaseRelativePath.stem().string();
                            const std::string scriptNodeLabel = scriptBaseName + " [Native Script]###ScriptPair_" + scriptBaseKey;
                            const std::string sourceAssetKey = "Assets/" + sourceRelativePath.generic_string();
                            const std::string headerAssetKey = "Assets/" + headerRelativePath.generic_string();
                            const bool scriptPairSelected =
                                (selectedNativeScriptAssetKey == sourceAssetKey) ||
                                (selectedNativeScriptAssetKey == headerAssetKey);
                            const ImGuiTreeNodeFlags scriptPairFlags = scriptPairSelected
                                ? (ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Selected)
                                : ImGuiTreeNodeFlags_DefaultOpen;
                            const bool scriptNodeOpen = ImGui::TreeNodeEx(scriptNodeLabel.c_str(), scriptPairFlags);

                            const bool selectedScriptPairWithoutDrag =
                                ImGui::IsItemHovered() &&
                                ImGui::IsMouseReleased(0) &&
                                (ImGui::GetDragDropPayload() == nullptr);
                            if (selectedScriptPairWithoutDrag)
                            {
                                state.MultiSelectedAssetKeys.clear();
                                state.MultiSelectedAssetKeys.push_back(sourceAssetKey);
                                state.SelectionAnchorAssetKey = sourceAssetKey;
                                selectedNativeScriptAssetKey = sourceAssetKey;
                                selectedPrefabAssetKey.clear();
                                selectedTextureAssetKey.clear();
                                selectedMaterialAssetKey.clear();
                                selectedTilesetAssetKey.clear();
                                selectedAudioMixerAssetKey.clear();
                                selectedInputActionsAssetKey.clear();
                                selectedAnimationClipAssetKey.clear();
                                selectedAnimatorControllerAssetKey.clear();
                                selectedEntity = entt::null;
                                cachedTextureAsset.reset();
                                cachedMaterialAsset.reset();
                            }

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                            {
                                onNativeScriptAssetActivated(sourceAssetKey);
                            }

                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Open Script") && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(sourceAssetKey);
                                if (ImGui::MenuItem("Rename Script Pair"))
                                {
                                    state.RenameAssetRelativePath = sourceRelativePath;
                                    CopyTextToBuffer(state.RenameAssetBuffer, scriptBaseName.c_str());
                                    state.RenameAssetAsNativeScriptPair = true;
                                    state.RenameAssetPopupPending = true;
                                }
                                if (ImGui::MenuItem("Delete Script Pair"))
                                {
                                    const bool removed = DeleteNativeScriptPairInAssets(assetsDirectory, sourceRelativePath);
                                    if (removed)
                                    {
                                        InvalidateProjectDirectoryCache();
                                        LT_INFO("Deleted native script pair {}", scriptBaseName);
                                    }
                                }
                                ImGui::EndPopup();
                            }

                            if (scriptNodeOpen)
                            {
                                const std::string headerItemLabel = scriptBaseName + " [.h Header]###ScriptPairHeader_" + scriptBaseKey;
                                const std::string sourceItemLabel = scriptBaseName + " [.cpp Source]###ScriptPairSource_" + scriptBaseKey;
                                const ImGuiTreeNodeFlags headerItemFlags =
                                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                    ((selectedNativeScriptAssetKey == headerAssetKey) ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
                                const ImGuiTreeNodeFlags sourceItemFlags =
                                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                    ((selectedNativeScriptAssetKey == sourceAssetKey) ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);

                                ImGui::TreeNodeEx(headerItemLabel.c_str(), headerItemFlags);
                                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr))
                                {
                                    state.MultiSelectedAssetKeys.clear();
                                    state.MultiSelectedAssetKeys.push_back(headerAssetKey);
                                    state.SelectionAnchorAssetKey = headerAssetKey;
                                    selectedNativeScriptAssetKey = headerAssetKey;
                                    selectedPrefabAssetKey.clear();
                                    selectedTextureAssetKey.clear();
                                    selectedMaterialAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
                                    selectedAnimationClipAssetKey.clear();
                                    selectedAnimatorControllerAssetKey.clear();
                                    selectedEntity = entt::null;
                                    cachedTextureAsset.reset();
                                    cachedMaterialAsset.reset();
                                }
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(headerAssetKey);

                                ImGui::TreeNodeEx(sourceItemLabel.c_str(), sourceItemFlags);
                                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr))
                                {
                                    state.MultiSelectedAssetKeys.clear();
                                    state.MultiSelectedAssetKeys.push_back(sourceAssetKey);
                                    state.SelectionAnchorAssetKey = sourceAssetKey;
                                    selectedNativeScriptAssetKey = sourceAssetKey;
                                    selectedPrefabAssetKey.clear();
                                    selectedTextureAssetKey.clear();
                                    selectedMaterialAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
                                    selectedAnimationClipAssetKey.clear();
                                    selectedAnimatorControllerAssetKey.clear();
                                    selectedEntity = entt::null;
                                    cachedTextureAsset.reset();
                                    cachedMaterialAsset.reset();
                                }
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(sourceAssetKey);
                                ImGui::TreePop();
                            }
                            continue;
                        }
                    }

                    const bool isTexture = IsTextureExtensionLower(entry.LowerExtension);
                    const bool isScene = IsSceneFileNameLower(entry.LowerFileName);
                    const bool isMaterial = IsMaterialFileNameLower(entry.LowerFileName);
                    const bool isTileset = IsTilesetFileNameLower(entry.LowerFileName);
                    const bool isAudioMixer = IsAudioMixerFileNameLower(entry.LowerFileName);
                    const bool isInputActions = IsInputActionsFileNameLower(entry.LowerFileName);
                    const bool isAnimationClip = IsAnimationClipFileNameLower(entry.LowerFileName);
                    const bool isAnimatorController = IsAnimatorControllerFileNameLower(entry.LowerFileName);
                    const bool isPrefab = IsPrefabFileNameLower(entry.LowerFileName);
                    const bool isShader = IsShaderExtensionLower(entry.LowerExtension);
                    const bool isAudio = IsAudioExtensionLower(entry.LowerExtension);
                    const bool isFont = IsFontExtensionLower(entry.LowerExtension);
                    const bool isNativeScriptFile = IsNativeScriptExtensionLower(entry.LowerExtension);
                    const bool hasPairedScriptFile = isNativeScriptFile
                        && ([&]() {
                               const std::filesystem::path scriptBaseRelativePath = entryRelativePath.parent_path() / entryRelativePath.stem();
                               const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseRelativePath.generic_string());
                               return scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                                      (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                          (kScriptPairHeaderBit | kScriptPairSourceBit);
                           })();
                    const std::string displayName = isNativeScriptFile
                        ? BuildScriptAssetDisplayName(entry.AbsolutePath)
                        : GetAssetDisplayName(entry.AbsolutePath);
                    const std::string treeLabel = displayName + "###" + fileName;
                    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                    const bool isPrimarySelected =
                        (isTexture && (selectedTextureAssetKey == assetKey)) ||
                        (isMaterial && (selectedMaterialAssetKey == assetKey)) ||
                        (isTileset && (selectedTilesetAssetKey == assetKey)) ||
                        (isAudioMixer && (selectedAudioMixerAssetKey == assetKey)) ||
                        (isInputActions && (selectedInputActionsAssetKey == assetKey)) ||
                        (isAnimationClip && (selectedAnimationClipAssetKey == assetKey)) ||
                        (isAnimatorController && (selectedAnimatorControllerAssetKey == assetKey)) ||
                        (isNativeScriptFile && (selectedNativeScriptAssetKey == assetKey)) ||
                        (isPrefab && (selectedPrefabAssetKey == assetKey));
                    const bool isMultiSelected = std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey) != state.MultiSelectedAssetKeys.end();
                    ImGui::TreeNodeEx(treeLabel.c_str(), (isPrimarySelected || isMultiSelected) ? (flags | ImGuiTreeNodeFlags_Selected) : flags);

                    const bool releasedOnItemWithoutDrag =
                        ImGui::IsItemHovered() &&
                        ImGui::IsMouseReleased(0) &&
                        (ImGui::GetDragDropPayload() == nullptr);
                    const auto clearAssetSelection = [&]() {
                        selectedTextureAssetKey.clear();
                        selectedMaterialAssetKey.clear();
                        selectedNativeScriptAssetKey.clear();
                        selectedPrefabAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        selectedAnimationClipAssetKey.clear();
                        selectedAnimatorControllerAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedTextureAsset.reset();
                        cachedMaterialAsset.reset();
                    };
                    const auto setPrimarySelectionForClickedAsset = [&]() {
                        clearAssetSelection();
                        if (isTexture)
                            selectedTextureAssetKey = assetKey;
                        else if (isMaterial)
                            selectedMaterialAssetKey = assetKey;
                        else if (isTileset)
                            selectedTilesetAssetKey = assetKey;
                        else if (isNativeScriptFile)
                            selectedNativeScriptAssetKey = assetKey;
                        else if (isPrefab)
                            selectedPrefabAssetKey = assetKey;
                        else if (isAudioMixer)
                            selectedAudioMixerAssetKey = assetKey;
                        else if (isInputActions)
                            selectedInputActionsAssetKey = assetKey;
                        else if (isAnimationClip)
                            selectedAnimationClipAssetKey = assetKey;
                        else if (isAnimatorController)
                            selectedAnimatorControllerAssetKey = assetKey;
                    };
                    if (releasedOnItemWithoutDrag)
                    {
                        const ImGuiIO& io = ImGui::GetIO();
                        const bool shiftPressed = io.KeyShift;
                        const bool controlPressed = io.KeyCtrl;

                        if (shiftPressed)
                        {
                            auto findIndex = [&](const std::string& key) -> int32_t {
                                for (size_t index = 0; index < directoryAssetKeys.size(); ++index)
                                {
                                    if (directoryAssetKeys[index] == key)
                                        return static_cast<int32_t>(index);
                                }
                                return -1;
                            };

                            const int32_t anchorIndex = findIndex(state.SelectionAnchorAssetKey);
                            const int32_t clickedIndex = findIndex(assetKey);
                            if (!controlPressed)
                                state.MultiSelectedAssetKeys.clear();

                            if (anchorIndex >= 0 && clickedIndex >= 0)
                            {
                                const int32_t minIndex = std::min(anchorIndex, clickedIndex);
                                const int32_t maxIndex = std::max(anchorIndex, clickedIndex);
                                for (int32_t index = minIndex; index <= maxIndex; ++index)
                                {
                                    const std::string& rangeKey = directoryAssetKeys[static_cast<size_t>(index)];
                                    if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), rangeKey) == state.MultiSelectedAssetKeys.end())
                                        state.MultiSelectedAssetKeys.push_back(rangeKey);
                                }
                            }
                            else if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey) == state.MultiSelectedAssetKeys.end())
                            {
                                state.MultiSelectedAssetKeys.push_back(assetKey);
                            }

                            setPrimarySelectionForClickedAsset();
                        }
                        else if (controlPressed)
                        {
                            const auto foundIt = std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey);
                            const bool wasSelected = foundIt != state.MultiSelectedAssetKeys.end();
                            if (wasSelected)
                            {
                                state.MultiSelectedAssetKeys.erase(foundIt);
                                if (state.MultiSelectedAssetKeys.empty())
                                    clearAssetSelection();
                            }
                            else
                            {
                                state.MultiSelectedAssetKeys.push_back(assetKey);
                                setPrimarySelectionForClickedAsset();
                            }
                            state.SelectionAnchorAssetKey = assetKey;
                        }
                        else
                        {
                            state.MultiSelectedAssetKeys.clear();
                            state.MultiSelectedAssetKeys.push_back(assetKey);
                            state.SelectionAnchorAssetKey = assetKey;
                            setPrimarySelectionForClickedAsset();
                        }
                    }

                    if (isTexture && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedTextureAssetKey = assetKey;
                    }
                    else if (isScene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onSceneActivated)
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedNativeScriptAssetKey.clear();
                        onSceneActivated(assetKey);
                    }
                    else if (isPrefab && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onPrefabOpened)
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedPrefabAssetKey = assetKey;
                        onPrefabOpened(assetKey);
                    }
                    else if (isMaterial && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedMaterialAssetKey = assetKey;
                    }
                    else if (isTileset && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedTilesetAssetKey = assetKey;
                    }
                    else if (isAudioMixer && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedAudioMixerAssetKey = assetKey;
                    }
                    else if (isAnimationClip && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedAnimationClipAssetKey = assetKey;
                    }
                    else if (isAnimatorController && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedAnimatorControllerAssetKey = assetKey;
                    }
                    else if (isNativeScriptFile && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedNativeScriptAssetKey = assetKey;
                        onNativeScriptAssetActivated(assetKey);
                    }

                    if (ImGui::BeginPopupContextItem())
                    {
                        if (isNativeScriptFile)
                        {
                            if (ImGui::MenuItem("Open Script") && onNativeScriptAssetActivated)
                                onNativeScriptAssetActivated(assetKey);
                            if (hasPairedScriptFile)
                                ImGui::Separator();
                        }

                        if (isScene)
                        {
                            if (ImGui::MenuItem("Open Scene") && onSceneActivated)
                            {
                                onSceneActivated(assetKey);
                            }
                            if (ImGui::MenuItem("Set As Default Scene") && onSetDefaultSceneRequested)
                            {
                                onSetDefaultSceneRequested(assetKey);
                            }
                            ImGui::Separator();
                        }
                        if (isPrefab)
                        {
                            if (ImGui::MenuItem("Open Prefab") && onPrefabOpened)
                                onPrefabOpened(assetKey);
                            if (ImGui::MenuItem("Instantiate Prefab") && onPrefabInstantiated)
                                onPrefabInstantiated(assetKey);
                            ImGui::Separator();
                        }

                        if (ImGui::MenuItem(hasPairedScriptFile ? "Rename Script Pair" : "Rename"))
                        {
                            state.RenameAssetRelativePath = entryRelativePath;
                            if (hasPairedScriptFile)
                                CopyTextToBuffer(state.RenameAssetBuffer, entry.AbsolutePath.stem().string().c_str());
                            else
                                CopyTextToBuffer(state.RenameAssetBuffer, displayName.c_str());
                            state.RenameAssetAsNativeScriptPair = hasPairedScriptFile;
                            state.RenameAssetPopupPending = true;
                        }
                        if (ImGui::MenuItem(hasPairedScriptFile ? "Delete Script Pair" : "Delete"))
                        {
                            bool removed = false;
                            if (hasPairedScriptFile)
                            {
                                removed = DeleteNativeScriptPairInAssets(assetsDirectory, entryRelativePath);
                            }
                            else
                            {
                                std::error_code deleteErrorCode;
                                removed = std::filesystem::remove(entry.AbsolutePath, deleteErrorCode);
                                if (removed && !deleteErrorCode)
                                {
                                    const std::filesystem::path metaPath = entry.AbsolutePath.parent_path() / (entry.AbsolutePath.filename().string() + ".meta");
                                    std::filesystem::remove(metaPath, deleteErrorCode);
                                    (void)Assets::AssetImportPipeline::ReimportChanged(true);
                                    InvalidateProjectDirectoryCache();
                                }
                            }
                            if (removed)
                                LT_INFO("Deleted asset {}", assetKey);
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        const bool draggingMultiSelection =
                            state.MultiSelectedAssetKeys.size() > 1 &&
                            std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey) != state.MultiSelectedAssetKeys.end();

                        if (draggingMultiSelection)
                        {
                            const std::string payloadText = EncodeAssetKeyListPayload(state.MultiSelectedAssetKeys);
                            if (!payloadText.empty())
                            {
                                ImGui::SetDragDropPayload(
                                    kAssetMultiSelectionPayload,
                                    payloadText.c_str(),
                                    static_cast<uint32_t>(payloadText.size() + 1),
                                    ImGuiCond_Once);
                            }
                            ImGui::Text("%zu assets", state.MultiSelectedAssetKeys.size());
                        }
                        else
                        {
                            if (isTexture)
                                ImGui::SetDragDropPayload(texturePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isScene)
                                ImGui::SetDragDropPayload(scenePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isPrefab)
                                ImGui::SetDragDropPayload(prefabPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isMaterial)
                                ImGui::SetDragDropPayload(materialPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isShader)
                                ImGui::SetDragDropPayload(shaderPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isAudio)
                                ImGui::SetDragDropPayload(audioPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isFont)
                                ImGui::SetDragDropPayload(fontPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else
                                ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);

                            ImGui::Text("%s", displayName.c_str());
                        }
                        ImGui::EndDragDropSource();
                    }
                }
            }
        }

        void DrawProjectFolderPopups(const std::filesystem::path& assetsDirectory,
                                     EditorProjectPanelState& state,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
                                     const std::function<void(const std::string&, const std::string&)>& onAssetRenamed)
        {
            if (state.FolderPopupPending == EditorProjectFolderPopup::Create)
            {
                ImGui::OpenPopup("CreateFolder");
                ImGui::SetNextWindowFocus();
                state.FolderPopupPending = EditorProjectFolderPopup::None;
                state.CreateFolderPopupOpen = true;
            }
            else if (state.FolderPopupPending == EditorProjectFolderPopup::Rename)
            {
                ImGui::OpenPopup("RenameFolder");
                ImGui::SetNextWindowFocus();
                state.FolderPopupPending = EditorProjectFolderPopup::None;
                state.RenameFolderPopupOpen = true;
            }

            if (ImGui::BeginPopupModal("CreateFolder", &state.CreateFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Folder");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("##Name",
                                                     state.FolderPopupBuffer.data(),
                                                     state.FolderPopupBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    if (state.FolderPopupBuffer[0] != '\0')
                    {
                        if (ProjectAssetOperations::CreateFolderInDirectory(assetsDirectory, state.FolderPopupParent, state.FolderPopupBuffer.data()))
                        {
                            InvalidateProjectDirectoryCache();
                            LT_INFO("Created folder {}", state.FolderPopupBuffer.data());
                        }
                        state.CreateFolderPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateFolderPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("RenameFolder", &state.RenameFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Rename Folder");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool rename = ImGui::InputText("##Name",
                                                     state.FolderPopupBuffer.data(),
                                                     state.FolderPopupBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
                {
                    if (state.FolderPopupBuffer[0] != '\0')
                    {
                        if (ProjectAssetOperations::RenameFolderInAssets(assetsDirectory, state.FolderPopupParent, state.FolderPopupBuffer.data()))
                        {
                            InvalidateProjectDirectoryCache();
                            LT_INFO("Renamed folder to {}", state.FolderPopupBuffer.data());
                        }
                        state.RenameFolderPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.RenameFolderPopupOpen = false;

                ImGui::EndPopup();
            }

            if (state.RenameAssetPopupPending)
            {
                ImGui::OpenPopup("RenameAsset");
                ImGui::SetNextWindowFocus();
                state.RenameAssetPopupPending = false;
                state.RenameAssetPopupOpen = true;
            }

            if (state.CreateNativeScriptPopupPending)
            {
                ImGui::OpenPopup("CreateNativeScriptAsset");
                ImGui::SetNextWindowFocus();
                state.CreateNativeScriptPopupPending = false;
                state.CreateNativeScriptPopupOpen = true;
            }

            if (state.CreateMaterialPopupPending)
            {
                ImGui::OpenPopup("CreateMaterialAsset");
                ImGui::SetNextWindowFocus();
                state.CreateMaterialPopupPending = false;
                state.CreateMaterialPopupOpen = true;
            }

            if (state.CreateTilesetPopupPending)
            {
                ImGui::OpenPopup("CreateTilesetAsset");
                ImGui::SetNextWindowFocus();
                state.CreateTilesetPopupPending = false;
                state.CreateTilesetPopupOpen = true;
            }

            if (state.CreateAudioMixerPopupPending)
            {
                ImGui::OpenPopup("CreateAudioMixerAsset");
                ImGui::SetNextWindowFocus();
                state.CreateAudioMixerPopupPending = false;
                state.CreateAudioMixerPopupOpen = true;
            }

            if (state.CreateAnimationClipPopupPending)
            {
                ImGui::OpenPopup("CreateAnimationClipAsset");
                ImGui::SetNextWindowFocus();
                state.CreateAnimationClipPopupPending = false;
                state.CreateAnimationClipPopupOpen = true;
            }

            if (state.CreateAnimatorControllerPopupPending)
            {
                ImGui::OpenPopup("CreateAnimatorControllerAsset");
                ImGui::SetNextWindowFocus();
                state.CreateAnimatorControllerPopupPending = false;
                state.CreateAnimatorControllerPopupOpen = true;
            }

            if (ImGui::BeginPopupModal("RenameAsset", &state.RenameAssetPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Rename Asset");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool rename = ImGui::InputText("##AssetName",
                                                     state.RenameAssetBuffer.data(),
                                                     state.RenameAssetBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
                {
                    if (state.RenameAssetBuffer[0] != '\0')
                    {
                        if (state.RenameAssetAsNativeScriptPair)
                        {
                            std::filesystem::path newHeaderRelativePath;
                            std::filesystem::path newSourceRelativePath;
                            if (RenameNativeScriptPairInAssets(
                                    assetsDirectory,
                                    state.RenameAssetRelativePath,
                                    state.RenameAssetBuffer.data(),
                                    newHeaderRelativePath,
                                    newSourceRelativePath))
                            {
                                if (onAssetRenamed)
                                {
                                    const std::filesystem::path oldBase = state.RenameAssetRelativePath.parent_path() / state.RenameAssetRelativePath.stem();
                                    onAssetRenamed("Assets/" + (oldBase.generic_string() + ".h"), "Assets/" + newHeaderRelativePath.generic_string());
                                    onAssetRenamed("Assets/" + (oldBase.generic_string() + ".cpp"), "Assets/" + newSourceRelativePath.generic_string());
                                }
                                LT_INFO("Renamed script pair to {}", state.RenameAssetBuffer.data());
                            }
                        }
                        else
                        {
                            const std::string oldAssetKey = "Assets/" + state.RenameAssetRelativePath.generic_string();
                            std::filesystem::path newAssetRelativePath;
                            if (ProjectAssetOperations::RenameAssetInAssets(
                                    assetsDirectory,
                                    state.RenameAssetRelativePath,
                                    state.RenameAssetBuffer.data(),
                                    &newAssetRelativePath))
                            {
                                InvalidateProjectDirectoryCache();
                                if (onAssetRenamed)
                                {
                                    const std::string newAssetKey = "Assets/" + newAssetRelativePath.generic_string();
                                    onAssetRenamed(oldAssetKey, newAssetKey);
                                }
                                LT_INFO("Renamed asset to {}", state.RenameAssetBuffer.data());
                            }
                        }
                        state.RenameAssetAsNativeScriptPair = false;
                        state.RenameAssetRelativePath.clear();
                        state.RenameAssetBuffer[0] = '\0';
                        state.RenameAssetPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    state.RenameAssetAsNativeScriptPair = false;
                    state.RenameAssetRelativePath.clear();
                    state.RenameAssetBuffer[0] = '\0';
                    state.RenameAssetPopupOpen = false;
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateNativeScriptAsset", &state.CreateNativeScriptPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Native Script");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Class Name",
                                                     state.CreateNativeScriptClassNameBuffer.data(),
                                                     state.CreateNativeScriptClassNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    std::string createdScriptAssetKey;
                    std::string createError;
                    if (CreateNativeScriptPairInAssets(
                            assetsDirectory,
                            state.CreateNativeScriptParentRelativePath,
                            state.CreateNativeScriptClassNameBuffer.data(),
                            createdScriptAssetKey,
                            createError))
                    {
                        LT_INFO("Created native script {}", createdScriptAssetKey);
                        state.CreateNativeScriptPopupOpen = false;
                    }
                    else if (!createError.empty())
                    {
                        LT_WARN("Failed to create native script: {}", createError);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateNativeScriptPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateMaterialAsset", &state.CreateMaterialPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Material");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateMaterialNameBuffer.data(),
                                                     state.CreateMaterialNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateMaterialNameBuffer.data();
                    if (!requestedName.empty() && onCreateMaterialRequested)
                    {
                        onCreateMaterialRequested(state.CreateMaterialParentRelativePath, requestedName);
                        state.CreateMaterialPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateMaterialPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateTilesetAsset", &state.CreateTilesetPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Tileset");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateTilesetNameBuffer.data(),
                                                     state.CreateTilesetNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateTilesetNameBuffer.data();
                    if (!requestedName.empty() && onCreateTilesetRequested)
                    {
                        onCreateTilesetRequested(state.CreateTilesetParentRelativePath, requestedName);
                        state.CreateTilesetPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateTilesetPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateAudioMixerAsset", &state.CreateAudioMixerPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Audio Mixer");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateAudioMixerNameBuffer.data(),
                                                     state.CreateAudioMixerNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateAudioMixerNameBuffer.data();
                    if (!requestedName.empty() && onCreateAudioMixerRequested)
                    {
                        onCreateAudioMixerRequested(state.CreateAudioMixerParentRelativePath, requestedName);
                        state.CreateAudioMixerPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateAudioMixerPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateAnimationClipAsset", &state.CreateAnimationClipPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Animation Clip");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateAnimationClipNameBuffer.data(),
                                                     state.CreateAnimationClipNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateAnimationClipNameBuffer.data();
                    if (!requestedName.empty() && onCreateAnimationClipRequested)
                    {
                        onCreateAnimationClipRequested(state.CreateAnimationClipParentRelativePath, requestedName);
                        state.CreateAnimationClipPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateAnimationClipPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateAnimatorControllerAsset", &state.CreateAnimatorControllerPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Animator Controller");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateAnimatorControllerNameBuffer.data(),
                                                     state.CreateAnimatorControllerNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateAnimatorControllerNameBuffer.data();
                    if (!requestedName.empty() && onCreateAnimatorControllerRequested)
                    {
                        onCreateAnimatorControllerRequested(state.CreateAnimatorControllerParentRelativePath, requestedName);
                        state.CreateAnimatorControllerPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateAnimatorControllerPopupOpen = false;

                ImGui::EndPopup();
            }
        }
    }

    void Draw(EditorProjectPanelState& state,
              entt::entity& selectedEntity,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              std::string& selectedPrefabAssetKey,
              std::string& selectedTilesetAssetKey,
              std::string& selectedAudioMixerAssetKey,
              std::string& selectedInputActionsAssetKey,
              std::string& selectedAnimationClipAssetKey,
              std::string& selectedAnimatorControllerAssetKey,
              const char* texturePayloadId,
              const char* audioPayloadId,
              const char* assetMovePayloadId,
              const char* scenePayloadId,
              const char* materialPayloadId,
              const char* prefabPayloadId,
              const char* shaderPayloadId,
              const char* fontPayloadId,
              const std::function<void(const std::string&)>& onSceneActivated,
              const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
              const std::function<void(entt::entity, const std::filesystem::path&)>& onCreatePrefabFromSceneEntityRequested,
              const std::function<void(const std::string&)>& onPrefabOpened,
              const std::function<void(const std::string&)>& onPrefabInstantiated,
              const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
              const std::function<void(const std::string&, const std::string&)>& onAssetRenamed,
              const std::function<void(const std::string&)>& onNativeScriptAssetActivated)
    {
        ImGui::Begin("Project");
        state.HoveredFolderRelativePathForExternalDrop.clear();

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Could not find Assets folder.");
            ImGui::End();
            return;
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        std::error_code errorCode;
        if (!std::filesystem::exists(assetsDirectory, errorCode) || !std::filesystem::is_directory(assetsDirectory, errorCode))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Assets directory not found.");
            ImGui::End();
            return;
        }

        if (ImGui::BeginPopupContextWindow("ProjectContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Folder"))
            {
                state.FolderPopupParent = "";
                state.FolderPopupPending = EditorProjectFolderPopup::Create;
                CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
            }
            if (ImGui::MenuItem("Create Scene") && onCreateSceneRequested)
                onCreateSceneRequested("");
            if (ImGui::MenuItem("Create Material"))
            {
                state.CreateMaterialParentRelativePath = "";
                CopyTextToBuffer(state.CreateMaterialNameBuffer, "New Material");
                state.CreateMaterialPopupPending = true;
            }
            if (ImGui::MenuItem("Create Tileset"))
            {
                state.CreateTilesetParentRelativePath = "";
                CopyTextToBuffer(state.CreateTilesetNameBuffer, "New Tileset");
                state.CreateTilesetPopupPending = true;
            }
            if (ImGui::MenuItem("Create Audio Mixer"))
            {
                state.CreateAudioMixerParentRelativePath = "";
                CopyTextToBuffer(state.CreateAudioMixerNameBuffer, "New Audio Mixer");
                state.CreateAudioMixerPopupPending = true;
            }
            if (ImGui::MenuItem("Create Animation Clip"))
            {
                state.CreateAnimationClipParentRelativePath = "";
                CopyTextToBuffer(state.CreateAnimationClipNameBuffer, "New Animation Clip");
                state.CreateAnimationClipPopupPending = true;
            }
            if (ImGui::MenuItem("Create Animator Controller"))
            {
                state.CreateAnimatorControllerParentRelativePath = "";
                CopyTextToBuffer(state.CreateAnimatorControllerNameBuffer, "New Animator Controller");
                state.CreateAnimatorControllerPopupPending = true;
            }
            if (ImGui::MenuItem("Create Native Script"))
            {
                state.CreateNativeScriptParentRelativePath = "";
                CopyTextToBuffer(state.CreateNativeScriptClassNameBuffer, "NewNativeScript");
                state.CreateNativeScriptPopupPending = true;
            }
            ImGui::EndPopup();
        }

        if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
            {
                state.HoveredFolderRelativePathForExternalDrop.clear();
            }

            if (ImGui::BeginPopupContextItem())
            {
                state.FolderPopupParent = "";
                if (ImGui::MenuItem("Create Folder"))
                {
                    state.FolderPopupPending = EditorProjectFolderPopup::Create;
                    CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
                }
                if (ImGui::MenuItem("Create Scene") && onCreateSceneRequested)
                    onCreateSceneRequested("");
                if (ImGui::MenuItem("Create Material"))
                {
                    state.CreateMaterialParentRelativePath = "";
                    CopyTextToBuffer(state.CreateMaterialNameBuffer, "New Material");
                    state.CreateMaterialPopupPending = true;
                }
                if (ImGui::MenuItem("Create Tileset"))
                {
                    state.CreateTilesetParentRelativePath = "";
                    CopyTextToBuffer(state.CreateTilesetNameBuffer, "New Tileset");
                    state.CreateTilesetPopupPending = true;
                }
                if (ImGui::MenuItem("Create Audio Mixer"))
                {
                    state.CreateAudioMixerParentRelativePath = "";
                    CopyTextToBuffer(state.CreateAudioMixerNameBuffer, "New Audio Mixer");
                    state.CreateAudioMixerPopupPending = true;
                }
                if (ImGui::MenuItem("Create Animation Clip"))
                {
                    state.CreateAnimationClipParentRelativePath = "";
                    CopyTextToBuffer(state.CreateAnimationClipNameBuffer, "New Animation Clip");
                    state.CreateAnimationClipPopupPending = true;
                }
                if (ImGui::MenuItem("Create Animator Controller"))
                {
                    state.CreateAnimatorControllerParentRelativePath = "";
                    CopyTextToBuffer(state.CreateAnimatorControllerNameBuffer, "New Animator Controller");
                    state.CreateAnimatorControllerPopupPending = true;
                }
                if (ImGui::MenuItem("Create Native Script"))
                {
                    state.CreateNativeScriptParentRelativePath = "";
                    CopyTextToBuffer(state.CreateNativeScriptClassNameBuffer, "NewNativeScript");
                    state.CreateNativeScriptPopupPending = true;
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMultiSelectionPayload))
                {
                    const std::vector<std::string> keys = ParseAssetKeyListPayload(payload->Data, payload->DataSize);
                    MoveAssetListToTargetFolder(keys, "");
                }
                else
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    MoveAssetOrFolderToTargetFolder(key, "");
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    MoveAssetOrFolderToTargetFolder(key, "");
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    MoveAssetOrFolderToTargetFolder(key, "");
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    MoveAssetOrFolderToTargetFolder(key, "");
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                {
                    const auto* entity = static_cast<const entt::entity*>(payload->Data);
                    if (entity && onCreatePrefabFromSceneEntityRequested)
                        onCreatePrefabFromSceneEntityRequested(*entity, "");
                }
                ImGui::EndDragDropTarget();
            }

            DrawAssetTree(assetsDirectory,
                          "",
                          state,
                          selectedEntity,
                          selectedTextureAssetKey,
                          cachedTextureAsset,
                          selectedMaterialAssetKey,
                          cachedMaterialAsset,
                          selectedNativeScriptAssetKey,
                          selectedPrefabAssetKey,
                          selectedTilesetAssetKey,
                          selectedAudioMixerAssetKey,
                          selectedInputActionsAssetKey,
                          selectedAnimationClipAssetKey,
                          selectedAnimatorControllerAssetKey,
                          texturePayloadId,
                          audioPayloadId,
                          assetMovePayloadId,
                          scenePayloadId,
                          materialPayloadId,
                          prefabPayloadId,
                          shaderPayloadId,
                          fontPayloadId,
                          onSceneActivated,
                          onCreateSceneRequested,
                          onCreateMaterialRequested,
                          onCreateTilesetRequested,
                          onCreateAudioMixerRequested,
                          onCreateAnimationClipRequested,
                          onCreateAnimatorControllerRequested,
                          onCreatePrefabFromSceneEntityRequested,
                          onPrefabOpened,
                          onPrefabInstantiated,
                          onSetDefaultSceneRequested,
                          onAssetRenamed,
                          onNativeScriptAssetActivated);
            ImGui::TreePop();
        }

        if (!state.PendingExternalDropPaths.empty() &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            const std::filesystem::path targetFolder = state.HoveredFolderRelativePathForExternalDrop;
            const bool importedAny = ProjectAssetOperations::ImportExternalPathsToFolder(
                state.PendingExternalDropPaths, targetFolder);
            if (importedAny)
            {
                InvalidateProjectDirectoryCache();
                LT_INFO("Imported {} external path(s) into Assets/{}",
                        state.PendingExternalDropPaths.size(),
                        targetFolder.generic_string());
            }
            state.PendingExternalDropPaths.clear();
        }

        DrawProjectFolderPopups(
            assetsDirectory,
            state,
            onCreateMaterialRequested,
            onCreateTilesetRequested,
            onCreateAudioMixerRequested,
            onCreateAnimationClipRequested,
            onCreateAnimatorControllerRequested,
            onAssetRenamed);
        ImGui::End();
    }
}
