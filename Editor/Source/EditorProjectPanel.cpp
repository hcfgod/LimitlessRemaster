#include "EditorProjectPanel.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorProjectPanel
{
    namespace
    {
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";

        void CopyTextToBuffer(std::array<char, 256>& destination, const char* source)
        {
            if (!source)
            {
                destination[0] = '\0';
                return;
            }

            std::snprintf(destination.data(), destination.size(), "%s", source);
        }

        bool IsTextureExtension(const std::filesystem::path& path)
        {
            const std::string extension = path.extension().string();
            if (extension.empty())
                return false;

            std::string extensionLower = extension;
            for (char& character : extensionLower)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

            return extensionLower == ".png" || extensionLower == ".jpg" || extensionLower == ".jpeg" ||
                   extensionLower == ".ppm" || extensionLower == ".pnm" || extensionLower == ".bmp" ||
                   extensionLower == ".tga" || extensionLower == ".gif";
        }

        bool IsSceneExtension(const std::filesystem::path& path)
        {
            std::string lowerFileName = path.filename().string();
            for (char& character : lowerFileName)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return lowerFileName.size() >= 11 && lowerFileName.rfind(".scene.json") == (lowerFileName.size() - 11);
        }

        bool IsMaterialExtension(const std::filesystem::path& path)
        {
            std::string lowerFileName = path.filename().string();
            for (char& character : lowerFileName)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            constexpr const char* materialSuffix = ".material.json";
            const std::string suffixString = materialSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsTilesetExtension(const std::filesystem::path& path)
        {
            std::string lowerFileName = path.filename().string();
            for (char& character : lowerFileName)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            constexpr const char* tilesetSuffix = ".tileset.json";
            const std::string suffixString = tilesetSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsAudioMixerExtension(const std::filesystem::path& path)
        {
            std::string lowerFileName = path.filename().string();
            for (char& character : lowerFileName)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            constexpr const char* audioMixerSuffix = ".audiomixer.json";
            const std::string suffixString = audioMixerSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsInputActionsExtension(const std::filesystem::path& path)
        {
            std::string lowerFileName = path.filename().string();
            for (char& character : lowerFileName)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            constexpr const char* inputActionsSuffix = ".inputactions.json";
            const std::string suffixString = inputActionsSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsPrefabExtension(const std::filesystem::path& path)
        {
            std::string lowerFileName = path.filename().string();
            for (char& character : lowerFileName)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            constexpr const char* prefabSuffix = ".prefab.json";
            const std::string suffixString = prefabSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsShaderExtension(const std::filesystem::path& path)
        {
            std::string lowerExtension = path.extension().string();
            for (char& character : lowerExtension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return lowerExtension == ".glsl";
        }

        bool IsAudioExtension(const std::filesystem::path& path)
        {
            std::string lowerExtension = path.extension().string();
            for (char& character : lowerExtension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return lowerExtension == ".wav" || lowerExtension == ".mp3" || lowerExtension == ".ogg" || lowerExtension == ".flac";
        }

        bool IsFontExtension(const std::filesystem::path& path)
        {
            std::string lowerExtension = path.extension().string();
            for (char& character : lowerExtension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return lowerExtension == ".ttf" || lowerExtension == ".otf";
        }

        bool IsNativeScriptExtension(const std::filesystem::path& path)
        {
            std::string lowerExtension = path.extension().string();
            for (char& character : lowerExtension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return lowerExtension == ".h" || lowerExtension == ".cpp";
        }

        std::filesystem::path GetPairedScriptRelativePath(const std::filesystem::path& scriptRelativePath)
        {
            std::filesystem::path pairPath = scriptRelativePath;
            std::string extension = pairPath.extension().string();
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            if (extension == ".cpp")
                pairPath.replace_extension(".h");
            else
                pairPath.replace_extension(".cpp");
            return pairPath;
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
                (void)Assets::AssetImportPipeline::ReimportChanged(true);
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

            std::vector<std::filesystem::path> entries;
            for (const auto& entry : std::filesystem::directory_iterator(currentDirectory, errorCode))
            {
                if (errorCode)
                    continue;

                const std::string name = entry.path().filename().string();
                if (name.empty() || name[0] == '.')
                    continue;
                if (name == "Cache")
                    continue;

                std::string extension = entry.path().extension().string();
                for (char& character : extension)
                    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                if (extension == ".meta")
                    continue;

                entries.push_back(entry.path());
            }

            std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
                const bool leftDirectory = std::filesystem::is_directory(left);
                const bool rightDirectory = std::filesystem::is_directory(right);
                if (leftDirectory != rightDirectory)
                    return leftDirectory;
                return left.filename().string() < right.filename().string();
            });

            std::unordered_set<std::string> renderedScriptBasePaths;
            for (const auto& entry : entries)
            {
                const std::string fileName = entry.filename().string();
                const bool isDirectory = std::filesystem::is_directory(entry);
                const std::string assetKey = "Assets/" + (relativePath / fileName).generic_string();
                const std::filesystem::path entryRelativePath = relativePath / fileName;

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
                                LT_INFO("Deleted folder {}", entryRelativePath.generic_string());
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                if (std::filesystem::path(key).extension().empty())
                                    ProjectAssetOperations::MoveFolderToFolder(key, entryRelativePath);
                                else
                                    ProjectAssetOperations::MoveAssetToFolder(key, entryRelativePath);
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                if (std::filesystem::path(key).extension().empty())
                                    ProjectAssetOperations::MoveFolderToFolder(key, entryRelativePath);
                                else
                                    ProjectAssetOperations::MoveAssetToFolder(key, entryRelativePath);
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                if (std::filesystem::path(key).extension().empty())
                                    ProjectAssetOperations::MoveFolderToFolder(key, entryRelativePath);
                                else
                                    ProjectAssetOperations::MoveAssetToFolder(key, entryRelativePath);
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                if (std::filesystem::path(key).extension().empty())
                                    ProjectAssetOperations::MoveFolderToFolder(key, entryRelativePath);
                                else
                                    ProjectAssetOperations::MoveAssetToFolder(key, entryRelativePath);
                            }
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
                    if (IsNativeScriptExtension(entry))
                    {
                        const std::filesystem::path scriptBaseRelativePath = entryRelativePath.parent_path() / entryRelativePath.stem();
                        const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                        if (renderedScriptBasePaths.find(scriptBaseKey) != renderedScriptBasePaths.end())
                            continue;

                        const std::filesystem::path headerRelativePath = scriptBaseRelativePath.string() + ".h";
                        const std::filesystem::path sourceRelativePath = scriptBaseRelativePath.string() + ".cpp";
                        const bool hasHeader = std::filesystem::exists(assetsDirectory / headerRelativePath);
                        const bool hasSource = std::filesystem::exists(assetsDirectory / sourceRelativePath);
                        if (hasHeader && hasSource)
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
                                selectedNativeScriptAssetKey = sourceAssetKey;
                                selectedPrefabAssetKey.clear();
                                selectedTextureAssetKey.clear();
                                selectedMaterialAssetKey.clear();
                                selectedTilesetAssetKey.clear();
                                selectedAudioMixerAssetKey.clear();
                                selectedInputActionsAssetKey.clear();
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
                                        LT_INFO("Deleted native script pair {}", scriptBaseName);
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
                                    selectedNativeScriptAssetKey = headerAssetKey;
                                    selectedPrefabAssetKey.clear();
                                    selectedTextureAssetKey.clear();
                                    selectedMaterialAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
                                    selectedEntity = entt::null;
                                    cachedTextureAsset.reset();
                                    cachedMaterialAsset.reset();
                                }
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(headerAssetKey);

                                ImGui::TreeNodeEx(sourceItemLabel.c_str(), sourceItemFlags);
                                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr))
                                {
                                    selectedNativeScriptAssetKey = sourceAssetKey;
                                    selectedPrefabAssetKey.clear();
                                    selectedTextureAssetKey.clear();
                                    selectedMaterialAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
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

                    const bool isTexture = IsTextureExtension(entry);
                    const bool isScene = IsSceneExtension(entry);
                    const bool isMaterial = IsMaterialExtension(entry);
                    const bool isTileset = IsTilesetExtension(entry);
                    const bool isAudioMixer = IsAudioMixerExtension(entry);
                    const bool isInputActions = IsInputActionsExtension(entry);
                    const bool isPrefab = IsPrefabExtension(entry);
                    const bool isShader = IsShaderExtension(entry);
                    const bool isAudio = IsAudioExtension(entry);
                    const bool isFont = IsFontExtension(entry);
                    const bool isNativeScriptFile = IsNativeScriptExtension(entry);
                    const std::filesystem::path pairedScriptRelativePath = isNativeScriptFile
                        ? GetPairedScriptRelativePath(entryRelativePath)
                        : std::filesystem::path{};
                    const bool hasPairedScriptFile = isNativeScriptFile && std::filesystem::exists(assetsDirectory / pairedScriptRelativePath);
                    const std::string displayName = isNativeScriptFile
                        ? BuildScriptAssetDisplayName(entry)
                        : GetAssetDisplayName(entry);
                    const std::string treeLabel = displayName + "###" + fileName;
                    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                    const bool isSelected =
                        (isTexture && (selectedTextureAssetKey == assetKey)) ||
                        (isMaterial && (selectedMaterialAssetKey == assetKey)) ||
                        (isTileset && (selectedTilesetAssetKey == assetKey)) ||
                        (isAudioMixer && (selectedAudioMixerAssetKey == assetKey)) ||
                        (isInputActions && (selectedInputActionsAssetKey == assetKey)) ||
                        (isNativeScriptFile && (selectedNativeScriptAssetKey == assetKey)) ||
                        (isPrefab && (selectedPrefabAssetKey == assetKey));
                    ImGui::TreeNodeEx(treeLabel.c_str(), isSelected ? (flags | ImGuiTreeNodeFlags_Selected) : flags);

                    const bool releasedOnItemWithoutDrag =
                        ImGui::IsItemHovered() &&
                        ImGui::IsMouseReleased(0) &&
                        (ImGui::GetDragDropPayload() == nullptr);
                    if (releasedOnItemWithoutDrag)
                    {
                        if (isTexture)
                        {
                            selectedTextureAssetKey = assetKey;
                            selectedMaterialAssetKey.clear();
                            selectedNativeScriptAssetKey.clear();
                            selectedPrefabAssetKey.clear();
                            selectedTilesetAssetKey.clear();
                            selectedAudioMixerAssetKey.clear();
                            selectedInputActionsAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedTextureAsset.reset();
                            cachedMaterialAsset.reset();
                        }
                        else if (isMaterial)
                        {
                            selectedMaterialAssetKey = assetKey;
                            selectedTextureAssetKey.clear();
                            selectedNativeScriptAssetKey.clear();
                            selectedPrefabAssetKey.clear();
                            selectedTilesetAssetKey.clear();
                            selectedAudioMixerAssetKey.clear();
                            selectedInputActionsAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedMaterialAsset.reset();
                            cachedTextureAsset.reset();
                        }
                        else if (isTileset)
                        {
                            selectedTilesetAssetKey = assetKey;
                            selectedTextureAssetKey.clear();
                            selectedMaterialAssetKey.clear();
                            selectedNativeScriptAssetKey.clear();
                            selectedPrefabAssetKey.clear();
                            selectedAudioMixerAssetKey.clear();
                            selectedInputActionsAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedMaterialAsset.reset();
                            cachedTextureAsset.reset();
                        }
                        else if (isNativeScriptFile)
                        {
                            selectedNativeScriptAssetKey = assetKey;
                            selectedTextureAssetKey.clear();
                            selectedMaterialAssetKey.clear();
                            selectedPrefabAssetKey.clear();
                            selectedTilesetAssetKey.clear();
                            selectedAudioMixerAssetKey.clear();
                            selectedInputActionsAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedMaterialAsset.reset();
                            cachedTextureAsset.reset();
                        }
                        else if (isPrefab)
                        {
                            selectedPrefabAssetKey = assetKey;
                            selectedTextureAssetKey.clear();
                            selectedMaterialAssetKey.clear();
                            selectedNativeScriptAssetKey.clear();
                            selectedTilesetAssetKey.clear();
                            selectedAudioMixerAssetKey.clear();
                            selectedInputActionsAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedMaterialAsset.reset();
                            cachedTextureAsset.reset();
                        }
                        else if (isAudioMixer)
                        {
                            selectedAudioMixerAssetKey = assetKey;
                            selectedTextureAssetKey.clear();
                            selectedMaterialAssetKey.clear();
                            selectedNativeScriptAssetKey.clear();
                            selectedPrefabAssetKey.clear();
                            selectedTilesetAssetKey.clear();
                            selectedInputActionsAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedMaterialAsset.reset();
                            cachedTextureAsset.reset();
                        }
                        else if (isInputActions)
                        {
                            selectedInputActionsAssetKey = assetKey;
                            selectedTextureAssetKey.clear();
                            selectedMaterialAssetKey.clear();
                            selectedNativeScriptAssetKey.clear();
                            selectedPrefabAssetKey.clear();
                            selectedTilesetAssetKey.clear();
                            selectedAudioMixerAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedMaterialAsset.reset();
                            cachedTextureAsset.reset();
                        }
                    }

                    if (isTexture && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        selectedTextureAssetKey = assetKey;
                        selectedMaterialAssetKey.clear();
                        selectedNativeScriptAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedTextureAsset.reset();
                        cachedMaterialAsset.reset();
                    }
                    else if (isScene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onSceneActivated)
                    {
                        selectedNativeScriptAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        onSceneActivated(assetKey);
                    }
                    else if (isPrefab && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onPrefabOpened)
                    {
                        selectedNativeScriptAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        selectedPrefabAssetKey = assetKey;
                        onPrefabOpened(assetKey);
                    }
                    else if (isMaterial && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        selectedMaterialAssetKey = assetKey;
                        selectedTextureAssetKey.clear();
                        selectedNativeScriptAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedMaterialAsset.reset();
                        cachedTextureAsset.reset();
                    }
                    else if (isTileset && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        selectedTilesetAssetKey = assetKey;
                        selectedTextureAssetKey.clear();
                        selectedMaterialAssetKey.clear();
                        selectedNativeScriptAssetKey.clear();
                        selectedPrefabAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedMaterialAsset.reset();
                        cachedTextureAsset.reset();
                    }
                    else if (isAudioMixer && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        selectedAudioMixerAssetKey = assetKey;
                        selectedTextureAssetKey.clear();
                        selectedMaterialAssetKey.clear();
                        selectedNativeScriptAssetKey.clear();
                        selectedPrefabAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedMaterialAsset.reset();
                        cachedTextureAsset.reset();
                    }
                    else if (isNativeScriptFile && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                    {
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
                                CopyTextToBuffer(state.RenameAssetBuffer, entry.stem().string().c_str());
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
                                removed = std::filesystem::remove(entry, deleteErrorCode);
                                if (removed && !deleteErrorCode)
                                {
                                    const std::filesystem::path metaPath = entry.parent_path() / (entry.filename().string() + ".meta");
                                    std::filesystem::remove(metaPath, deleteErrorCode);
                                    (void)Assets::AssetImportPipeline::ReimportChanged(true);
                                }
                            }
                            if (removed)
                                LT_INFO("Deleted asset {}", assetKey);
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
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
                            LT_INFO("Created folder {}", state.FolderPopupBuffer.data());
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
                            LT_INFO("Renamed folder to {}", state.FolderPopupBuffer.data());
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
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            ProjectAssetOperations::MoveFolderToFolder(key, "");
                        else
                            ProjectAssetOperations::MoveAssetToFolder(key, "");
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            ProjectAssetOperations::MoveFolderToFolder(key, "");
                        else
                            ProjectAssetOperations::MoveAssetToFolder(key, "");
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            ProjectAssetOperations::MoveFolderToFolder(key, "");
                        else
                            ProjectAssetOperations::MoveAssetToFolder(key, "");
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            ProjectAssetOperations::MoveFolderToFolder(key, "");
                        else
                            ProjectAssetOperations::MoveAssetToFolder(key, "");
                    }
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
                LT_INFO("Imported {} external path(s) into Assets/{}",
                        state.PendingExternalDropPaths.size(),
                        targetFolder.generic_string());
            }
            state.PendingExternalDropPaths.clear();
        }

        DrawProjectFolderPopups(assetsDirectory, state, onCreateMaterialRequested, onCreateTilesetRequested, onCreateAudioMixerRequested, onAssetRenamed);
        ImGui::End();
    }
}
