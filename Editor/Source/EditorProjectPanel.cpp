#include "EditorProjectPanel.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Limitless::EditorProjectPanel
{
    namespace
    {
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

        bool IsShaderExtension(const std::filesystem::path& path)
        {
            std::string lowerExtension = path.extension().string();
            for (char& character : lowerExtension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return lowerExtension == ".glsl";
        }

        std::string GetAssetDisplayName(const std::filesystem::path& path)
        {
            const std::string fileName = path.filename().string();
            std::string lowerFileName = fileName;
            for (char& character : lowerFileName)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

            constexpr std::array<const char*, 3> compoundSuffixes = {
                ".scene.json",
                ".material.json",
                ".inputactions.json"
            };

            for (const char* suffix : compoundSuffixes)
            {
                const std::string suffixString = suffix;
                if (lowerFileName.size() >= suffixString.size() &&
                    lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size()))
                {
                    return fileName.substr(0, fileName.size() - suffixString.size());
                }
            }

            return path.stem().string();
        }

        void DrawAssetTree(const std::filesystem::path& assetsDirectory,
                           const std::filesystem::path& relativePath,
                           EditorProjectPanelState& state,
                           entt::entity& selectedEntity,
                           std::string& selectedTextureAssetKey,
                           Assets::TextureAsset::Ptr& cachedTextureAsset,
                           std::string& selectedMaterialAssetKey,
                           Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                           const char* texturePayloadId,
                           const char* assetMovePayloadId,
                           const char* scenePayloadId,
                           const char* materialPayloadId,
                           const char* shaderPayloadId,
                           const std::function<void(const std::string&)>& onSceneActivated,
                           const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested)
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
                                      texturePayloadId,
                                      assetMovePayloadId,
                                      scenePayloadId,
                                      materialPayloadId,
                                      shaderPayloadId,
                                      onSceneActivated,
                                      onCreateSceneRequested);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    const bool isTexture = IsTextureExtension(entry);
                    const bool isScene = IsSceneExtension(entry);
                    const bool isMaterial = IsMaterialExtension(entry);
                    const bool isShader = IsShaderExtension(entry);
                    const std::string displayName = GetAssetDisplayName(entry);
                    const std::string treeLabel = displayName + "###" + fileName;
                    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                    const bool isSelected =
                        (isTexture && (selectedTextureAssetKey == assetKey)) ||
                        (isMaterial && (selectedMaterialAssetKey == assetKey));
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
                            selectedEntity = entt::null;
                            cachedTextureAsset.reset();
                            cachedMaterialAsset.reset();
                        }
                        else if (isMaterial)
                        {
                            selectedMaterialAssetKey = assetKey;
                            selectedTextureAssetKey.clear();
                            selectedEntity = entt::null;
                            cachedMaterialAsset.reset();
                            cachedTextureAsset.reset();
                        }
                    }

                    if (isTexture && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        selectedTextureAssetKey = assetKey;
                        selectedMaterialAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedTextureAsset.reset();
                        cachedMaterialAsset.reset();
                    }
                    else if (isScene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onSceneActivated)
                    {
                        onSceneActivated(assetKey);
                    }
                    else if (isMaterial && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        selectedMaterialAssetKey = assetKey;
                        selectedTextureAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedMaterialAsset.reset();
                        cachedTextureAsset.reset();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        if (isTexture)
                            ImGui::SetDragDropPayload(texturePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        else if (isScene)
                            ImGui::SetDragDropPayload(scenePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        else if (isMaterial)
                            ImGui::SetDragDropPayload(materialPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        else if (isShader)
                            ImGui::SetDragDropPayload(shaderPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        else
                            ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);

                        ImGui::Text("%s", displayName.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }
        }

        void DrawProjectFolderPopups(const std::filesystem::path& assetsDirectory, EditorProjectPanelState& state)
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
        }
    }

    void Draw(EditorProjectPanelState& state,
              entt::entity& selectedEntity,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              const char* texturePayloadId,
              const char* assetMovePayloadId,
              const char* scenePayloadId,
              const char* materialPayloadId,
              const char* shaderPayloadId,
              const std::function<void(const std::string&)>& onSceneActivated,
              const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested)
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
                          texturePayloadId,
                          assetMovePayloadId,
                          scenePayloadId,
                          materialPayloadId,
                          shaderPayloadId,
                          onSceneActivated,
                          onCreateSceneRequested);
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

        DrawProjectFolderPopups(assetsDirectory, state);
        ImGui::End();
    }
}
